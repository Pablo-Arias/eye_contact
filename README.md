# Eye-contact GStreamer Plug-in (Docker edition)

This repository ships a **ready-to-run Docker stack** that:
1. Compiles `libgsteyecontact.so` from source.
2. Packages it together with the NVIDIA Maxine AR runtime.
3. Lets you inject the element into any GStreamer pipeline – most
   notably DuckSoup’s `videoFx` hook just like *mozza* does  [oai_citation:2‡GitHub](https://github.com/ducksouplab/mozza).

> **Prerequisites on the host**
> * NVIDIA driver ≥ 535.xx
> * NVIDIA Container Toolkit ( `nvidia-docker` )  [oai_citation:3‡NVIDIA Docs](https://docs.nvidia.com/ai-enterprise/deployment/vmware/latest/docker.html?utm_source=chatgpt.com)  
> * A Turing-class GPU or newer (RTX 2000+), the same requirement Maxine lists  [oai_citation:4‡NVIDIA Developer](https://developer.nvidia.com/maxine?utm_source=chatgpt.com).


## Download maxine sdk
```bash
# 1. fetch the official installer (note: *no* login needed for this step)
wget -qO ngccli_linux.zip https://org.ngc.nvidia.com/setup/installers/cli/linux

# 2. run the bundled installer script (copies *all* libs, sets wrapper)
unzip -q ngccli_linux.zip
sudo ./ngccli_install.sh -n     # -n = non-interactive, installs to /usr/local/ngc

# 3. configure once with your API key
ngc config set                  # paste key → press <Enter> through the prompts
ngc --version                   # should now print “NGC CLI x.y.z”
```
If wget is blocked by a firewall, just log in to NGC in a browser and click Setup → CLI Install → “AMD64 Linux” to download the same zip manually [here](https://org.ngc.nvidia.com/setup/installers/cli).


Once you downoad it, put it in your folder.

Then:
```bash
# 1 - make the parent directory
sudo mkdir -p /usr/local/ngc                     # -p = make all missing parents

# 2 - move the whole bundle (keep its internal tree intact!)
sudo mv ~/repos/ngc-cli /usr/local/ngc/ngc-cli

# 3 - expose the launcher
sudo ln -sf /usr/local/ngc/ngc-cli/ngc /usr/local/bin/ngc

# 4 - test
ngc --version
```
After step 4 you should see something like NGC CLI 3.43.0. If you do, the CLI is installed correctly and will never complain about libpython3.11.so.1.0, because that library now lives next to the binary inside /usr/local/ngc/ngc-cli. mv itself didn’t fail—the target path just didn’t exist  ￼.


Now configure CLI:
```bash
ngc config set

#Download Maxine —you need a licence to do this
ngc registry resource download-version \
    "nvidia/maxine/maxine_linux_ar_sdk:2.0"
```



### Quick start

```bash
# 1  Drop the SDK tar-ball once
mkdir -p docker/deps
cp ~/Downloads/maxine-ar-sdk-linux-x86_64-2.0.tar.gz docker/deps/

# 2  Build everything
docker compose build        # ~8 min first time

# 3  Verify
docker compose run --rm dev gst-inspect-1.0 eyecontact


# Using the image in production
```bash
docker run --rm --gpus all \
  -v "$(pwd)/media":/media \
  ducksouplab/eyecontact:latest \
  gst-launch-1.0 filesrc location=/media/in.mp4 ! decodebin \
    ! eyecontact redirect=true batch-size=1 ! autovideosink
```

If you embed it in DuckSoup, just add videoFx="eyecontact redirect=true name=eye" to the peer options – the mechanism is identical to mozza  ￼.

---

### `meson.build` (top level)

```meson
project('eyecontact', 'cpp', license : 'LGPL-2.1-or-later',version : '1.0', default_options : ['cpp_std=c++17'])

gst        = dependency('gstreamer-1.0')
gstvideo   = dependency('gstreamer-video-1.0')
cuda       = dependency('cuda')

nvar_root  = get_option('nvar_root', fallback : '/opt/nvidia/ar_sdk')
nvar_dep   = declare_dependency(
  include_directories : include_directories(join_paths(nvar_root, 'include')),
  link_args : ['-L' + join_paths(nvar_root, 'lib'),
               '-lnvar', '-lnvcvapi'])

shared_library('gsteyecontact',
  sources              : 'src/gsteyecontact.cpp',
  dependencies         : [gst, gstvideo, cuda, nvar_dep],
  install              : true,
  install_dir          : join_paths(get_option('libdir'), 'gstreamer-1.0'),
  name_prefix          : '')