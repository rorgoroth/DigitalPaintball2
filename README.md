## STATUS
### Windows: Build broken.

### Linux: Builds, doesn't run.

#
## Deps
**Alpine**:  
```apk add alsa-lib-dev glu-dev jpeg-dev libjpeg-turbo-dev libogg-dev libpng-dev libvorbis-dev libxxf86dga-dev libxxf86vm-dev linux-headers mesa-dev sdl-dev```

**Ubuntu**:  
```sudo apt-get install libasound2-dev libgl1-mesa-dev libjpeg-dev libvorbis-dev libxxf86vm-dev mesa-common-dev```

**Fedora**:  
```dnf in alsa-lib-devel libjpeg-devel libX11-devel libXxf86dga-devel libXxf86vm-devel SDL*-devel X11-devel xorg-x11-server-devel```

#
## Compile
```make release```