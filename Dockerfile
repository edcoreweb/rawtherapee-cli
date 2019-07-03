# Start with our base image
FROM ubuntu:18.04

# Install all the system dependencies
RUN apt-get update && apt-get install -y \
      build-essential \
      cmake \
      wget \
      curl \
      git \
      vim \
      libcanberra-gtk3-dev \
      libexiv2-dev \
      libexpat-dev \
      libfftw3-dev \
      libglibmm-2.4-dev \
      libgtk-3-dev \
      libgtkmm-3.0-dev \
      libiptcdata0-dev \
      libjpeg-dev \
      liblcms2-dev \
      liblensfun-dev \
      libpng-dev \
      librsvg2-dev \
      libsigc++-2.0-dev \
      libtiff5-dev \
      zlib1g-dev \
    && rm -r /var/lib/apt/lists/*

WORKDIR /root

# Install rawtherapee
RUN wget https://raw.githubusercontent.com/Beep6581/RawTherapee/dev/tools/build-rawtherapee -O build-rawtherapee
RUN chmod +x build-rawtherapee
RUN ./build-rawtherapee

CMD ["/bin/bash"]
