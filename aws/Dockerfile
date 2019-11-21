# Start with our base image
FROM amazonlinux:2

# Install all the system dependencies
RUN yum update -y && yum install -y wget
RUN yum groupinstall -y "Development Tools"

RUN wget http://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
RUN rpm -ivh epel-release-latest-7.noarch.rpm

RUN yum install -y \
	  wget \
	  curl \
	  git \
	  vim \
	  libcanberra-devel \
	  exiv2-devel \ 
	  expat-devel \
	  fftw-devel \
	  glibmm24-devel \
	  gtk3-devel \
	  gtkmm30-devel \
	  libiptcdata-devel \
	  libjpeg-turbo-devel \
	  lcms2-devel \
	  lensfun-devel \
	  libpng12-devel \
	  librsvg2-devel \
	  libsigc++20-devel \
	  zlib-devel
	  
# CMake 3.6
RUN yum remove cmake -y \
	  && wget https://cmake.org/files/v3.6/cmake-3.6.2.tar.gz \
	  && tar -zxvf cmake-3.6.2.tar.gz \
	  && cd cmake-3.6.2 \
	  && ./bootstrap \
	  && make \
	  && make install \
	  && su
	  
# libtiff  4.0.9
RUN yum remove libtiff-devel -y \
	  && wget http://download.osgeo.org/libtiff/tiff-4.0.9.tar.gz \
	  && tar -zxvf tiff-4.0.9.tar.gz \
	  && cd tiff-4.0.9 \
	  && ./configure --libdir=/lib64 \
	  && make \
	  && make install \
	  && su
	  
# Install rawtherapee
RUN wget https://raw.githubusercontent.com/Beep6581/RawTherapee/dev/tools/build-rawtherapee -O build-rawtherapee
RUN chmod +x build-rawtherapee
RUN ./build-rawtherapee -b

# Build our custom cli
RUN rm -rf /root/programs/rawtherapee
COPY CMakeLists.txt /root/programs/code-rawtherapee/rtgui/CMakeLists.txt
COPY cli.cc /root/programs/code-rawtherapee/rtgui/main-cli-custom.cc
RUN ./build-rawtherapee -b

WORKDIR /root

CMD ["/bin/bash"]
