/*
 * gsteyecontact.cpp  –  GStreamer video filter wrapping NVIDIA Maxine
 *                      “Eye Contact / Gaze Redirect” feature.
 *
 * Build: see top-level README.md
 * Licence: LGPL-2.1-or-later
 */

 #include <gst/gst.h>
 #include <gst/video/video.h>
 #include <gst/video/gstvideofilter.h>
 
 #include <NvAR.h>          // Maxine AR-SDK
 #include <nvCVImage.h>
 #include <cuda_runtime.h>
 
 #include <mutex>
 
 /* ------------------------------------------------------------------------ */
 /*  Boiler-plate GObject stuff                                              */
 /* ------------------------------------------------------------------------ */
 
 GST_DEBUG_CATEGORY_STATIC (gst_eye_contact_debug);
 #define GST_CAT_DEFAULT gst_eye_contact_debug
 #define PACKAGE "eyecontact"
 #define GST_TYPE_EYE_CONTACT (gst_eye_contact_get_type())
 G_DECLARE_FINAL_TYPE (GstEyeContact, gst_eye_contact,
                       GST, EYE_CONTACT, GstVideoFilter)
 
 struct _GstEyeContact
 {
   GstVideoFilter parent;
 
   /* Maxine runtime */
   NvAR_FeatureHandle handle { nullptr };
   CUstream            stream { 0 };
 
   /* User-configurable */
   gboolean redirect { TRUE };
   guint     batch   { 1 };
   gchar    *model_dir { nullptr };
 
   std::mutex lock;
 };
 
 G_DEFINE_TYPE (GstEyeContact, gst_eye_contact, GST_TYPE_VIDEO_FILTER)
 
 /* ---------------- property IDs ---------------- */
 enum
 {
   PROP_0,
   PROP_REDIRECT,
   PROP_BATCH,
   PROP_MODEL_DIR
 };
 
 /* ---------------- utils ---------------- */
 static void
 wrap_gstframe_cuda (const GstVideoFrame &f, NvCVImage &out)
 {
   out.bufferType   = NVCV_IMAGE_TYPE_CUDA;
   out.pixelType    = NVCV_ARGB_32U;                 /* SDK converts internally */
   out.width        = GST_VIDEO_FRAME_WIDTH (&f);
   out.height       = GST_VIDEO_FRAME_HEIGHT(&f);
   out.pitch        = GST_VIDEO_FRAME_PLANE_STRIDE(&f, 0);
   out.pixelData    = GST_VIDEO_FRAME_PLANE_DATA (&f, 0);
 }
 
 /* ------------------------------------------------------------------------ */
 /*  GObject virtuals                                                        */
 /* ------------------------------------------------------------------------ */
 static void
 gst_eye_contact_set_property (GObject *obj,
                               guint    id,
                               const GValue *val,
                               GParamSpec *)
 {
   auto *self = GST_EYE_CONTACT (obj);
   switch (id) {
   case PROP_REDIRECT:
     self->redirect = g_value_get_boolean (val);
     break;
   case PROP_BATCH:
     self->batch = g_value_get_uint (val);
     break;
   case PROP_MODEL_DIR:
     g_free (self->model_dir);
     self->model_dir = g_value_dup_string (val);
     break;
   default:
     G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, nullptr);
   }
 }
 
 static void
 gst_eye_contact_get_property (GObject *obj,
                               guint    id,
                               GValue  *val,
                               GParamSpec *)
 {
   auto *self = GST_EYE_CONTACT (obj);
   switch (id) {
   case PROP_REDIRECT:
     g_value_set_boolean (val, self->redirect);
     break;
   case PROP_BATCH:
     g_value_set_uint (val, self->batch);
     break;
   case PROP_MODEL_DIR:
     g_value_set_string (val, self->model_dir);
     break;
   default:
     G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, nullptr);
   }
 }
 
 /* ---------------- start / stop ---------------- */
 static gboolean
 gst_eye_contact_start (GstBaseTransform *trans)
 {
   auto *self = GST_EYE_CONTACT (trans);
 
   if (cudaStreamCreate (&self->stream) != cudaSuccess) {
     GST_ERROR_OBJECT (self, "Failed to create CUDA stream");
     return FALSE;
   }
 
   if (NvAR_Create (NvAR_Feature_GazeRedirect, &self->handle) != NVAR_Success) {
     GST_ERROR_OBJECT (self, "NvAR_Create failed");
     return FALSE;
   }
 
   NvAR_SetCudaStream (self->handle,
                       NvAR_Parameter_Config(CUDAStream),
                       self->stream);
   NvAR_SetU32 (self->handle,
                NvAR_Parameter_Config(BatchSize),
                self->batch);
   NvAR_SetU32 (self->handle,
                NvAR_Parameter_Config(GazeRedirect),
                self->redirect ? 1u : 0u);
 
   if (self->model_dir)
     NvAR_SetString (self->handle,
                     NvAR_Parameter_Config(ModelDir),
                     self->model_dir);
 
   if (NvAR_Load (self->handle) != NVAR_Success) {
     GST_ERROR_OBJECT (self, "NvAR_Load failed");
     return FALSE;
   }
 
   GST_INFO_OBJECT (self, "EyeContact initialised");
   return TRUE;
 }
 
 static gboolean
 gst_eye_contact_stop (GstBaseTransform *trans)
 {
   auto *self = GST_EYE_CONTACT (trans);
   if (self->handle)
     NvAR_Destroy (self->handle);
   if (self->stream)
     cudaStreamDestroy (self->stream);
   return TRUE;
 }
 
 /* ---------------- per-frame ---------------- */
 static GstFlowReturn
 gst_eye_contact_transform_frame_ip (GstVideoFilter *vf,
                                     GstVideoFrame  *frame)
 {
   auto *self = GST_EYE_CONTACT (vf);
   std::lock_guard<std::mutex> guard (self->lock);
 
   NvCVImage in{}, out{};
   wrap_gstframe_cuda (*frame, in);
   wrap_gstframe_cuda (*frame, out);     /* in-place overwrite */
 
   NvAR_SetObject (self->handle, NvAR_Parameter_Input(Image),
                   &in, sizeof(NvCVImage));
   NvAR_SetObject (self->handle, NvAR_Parameter_Output(Image),
                   &out, sizeof(NvCVImage));
 
   if (NvAR_Run (self->handle) != NVAR_Success)
     GST_WARNING_OBJECT (self, "NvAR_Run failed");
 
   return GST_FLOW_OK;
 }
 
 /* ------------------------------------------------------------------------ */
 /*  class_init / init                                                       */
 /* ------------------------------------------------------------------------ */
 static void
 gst_eye_contact_class_init (GstEyeContactClass *klass)
 {
   auto *oclass      = G_OBJECT_CLASS (klass);
   auto *vfilter     = GST_VIDEO_FILTER_CLASS (klass);
   auto *btransform  = GST_BASE_TRANSFORM_CLASS (klass);
 
   oclass->set_property = gst_eye_contact_set_property;
   oclass->get_property = gst_eye_contact_get_property;
 
   g_object_class_install_property (oclass, PROP_REDIRECT,
     g_param_spec_boolean ("redirect", "Redirect",
       "Apply gaze redirection (FALSE → estimate only)",
       TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
   g_object_class_install_property (oclass, PROP_BATCH,
     g_param_spec_uint ("batch-size", "Batch size",
       "Batch size given to the SDK (1–8)",
       1, 8, 1,
       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
   g_object_class_install_property (oclass, PROP_MODEL_DIR,
     g_param_spec_string ("model-dir", "Model directory",
       "Folder containing the Eye-contact TensorRT models",
       nullptr,
       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
 
   gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
     "Eye contact (NVIDIA Maxine)", "Filter/Video/Effect/AI",
     "Redirects gaze toward camera in real time",
     "Pablo Arias Sarah");
 
   vfilter->transform_frame_ip = gst_eye_contact_transform_frame_ip;
   btransform->start = gst_eye_contact_start;
   btransform->stop  = gst_eye_contact_stop;
 }
 
 static void
 gst_eye_contact_init (GstEyeContact *) {}
 
 /* ------------------------------------------------------------------------ */
 /*  Plugin entry-point                                                      */
 /* ------------------------------------------------------------------------ */
 static gboolean
 plugin_init (GstPlugin *plugin)
 {
   GST_DEBUG_CATEGORY_INIT (gst_eye_contact_debug, PACKAGE, 0,
                            "Eye contact");
   return gst_element_register (plugin, "eyecontact",
                                GST_RANK_NONE, GST_TYPE_EYE_CONTACT);
 }
 
 GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
                    GST_VERSION_MINOR,
                    eyecontact,
                    "Eye contact gaze redirection (NVIDIA Maxine)",
                    plugin_init,
                    "1.0",
                    "LGPL",
                    PACKAGE,
                    "https://github.com/you/eyecontact-gst-plugin")