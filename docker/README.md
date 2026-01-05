## Notice
Kernels built with this Docker flow use a different kernel name from the
Particle image. Use this workflow for development/testing only.

## Prerequisite
Build the image first:
```
docker build -t ubuntu-24.04-kernel-builder ./docker
```

## Full build (clean)
```
docker run -it --rm -u $(id -u):$(id -g) -v $PWD:/home/ubuntu/workspace \
    ubuntu-24.04-kernel-builder /home/ubuntu/workspace/docker/build-kernel.sh cleanbuild
```

## Incremental build
```
docker run -it --rm -u $(id -u):$(id -g) -v $PWD:/home/ubuntu/workspace \
    ubuntu-24.04-kernel-builder /home/ubuntu/workspace/docker/build-kernel.sh
```

## Module build (in-tree module dir)
```
docker run -it --rm -u $(id -u):$(id -g) -v $PWD:/home/ubuntu/workspace \
    ubuntu-24.04-kernel-builder bash /home/ubuntu/workspace/docker/build-module.sh <module-dir>
```

Example:
```
docker run -it --rm -u $(id -u):$(id -g) -v $PWD:/home/ubuntu/workspace \
    ubuntu-24.04-kernel-builder bash /home/ubuntu/workspace/docker/build-module.sh sound/soc/qcom/qdsp6
```

## Outputs
Debian packages are copied to: docker/build

## Note on incremental build time
Even for incremental builds, scripts/package/builddeb generates multiple debs
by default (linux-libc-dev, linux-headers, linux-image-*-dbg), which can still
take a long time.

## Fix (limit to linux-image only)
```
diff --git a/scripts/package/builddeb b/scripts/package/builddeb
index d05c7567795f..8f455df68b3a 100755
--- a/scripts/package/builddeb
+++ b/scripts/package/builddeb
@@ -165,7 +165,7 @@ install_libc_headers () {
 
 rm -f debian/files
 
-packages_enabled=$(dh_listpackages)
+packages_enabled=$(dh_listpackages | tr ' ' '\n' | awk -v kr="${KERNELRELEASE}" '$0 == "linux-image-" kr {print}')
 
 for package in ${packages_enabled}
 do
```
