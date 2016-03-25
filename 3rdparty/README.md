= CMake external projects to build krita's dependencies on Linux, Windows or OSX =

If you need to build Krita's dependencies for the following reasons:

* you develop on Windows and aren't using Emerge
* you develop on OSX and aren't using Homebrew
* you want to build a generic, distro-agnostic version of Krita for Linux
* you develop on Linux, but some dependencies aren't available for your distribution

and you know what you're doing, you can use the following guide to build
the dependencies that Krita needs.

If you develop on Linux and your distribution has the dependencies available,

YOU DO NOT NEED THIS GUIDE AND YOU SHOULD STOP READING NOW

Otherwise you risk major confusion.

== Prerequisites ==

Note: on all operating systems the entire procedure is done in a terminal window.

1. git: https://git-scm.com/downloads. Make sure git is in your path
2. cmake 3.3.2: https://cmake.org/download/. Make sure cmake is in your path.
3. Make sure you have a compiler:
    * Linux: gcc, minimum version 4.8
    * OSX: clang, you need to install xcode for this
    * Windows: MSVC 2015 Community Edition: https://www.visualstudio.com/en-us/products/visual-studio-community-vs.aspx
4. If you compile Qt on Windows, you will also need Python 2.7: https://www.python.org/download/releases/2.7/. Make sure to have python.exe in you path.

== Setup your environment ==

Windows Only:
    When launching the commands from the console, run first "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\amd64\vcvars64.bat" in it (include the quotes). This way all the environment variables for the compiler will be ready.

== Prepare your directory layout ==

1. Make a toplevel build directory, say $HOME/dev or c:\dev. We'll refer to this directory as BUILDROOT. You can use a variable for this, on WINDOWS %BUILDROOT%, on OSX and Linux $BUILDROOT. 

2. Checkout krita in BUILDROOT
    cd BUILDROOT
    git clone git://anongit.kde.org/krita.git
3. Create the build directory
    mkdir BUILDROOT/b
4. Create the downloads directory
    mkdir BUILDROOT/d
5. Create the install directory
    mkdir BUILDROOT/i

== Qt ==

Install Qt. Either build from source or with the qt.io installer. 

Either way, make sure qmake is in your environment path. If you are using Qt 5.6 for example, this is the default location for Windows:
C:\Qt\Qt5.6.0\5.6\msvc2015_64\bin, when using the qt.io installer.

When building from source, you need the following modules:
 
* qtbase
* qtdeclarative
* qtimageformats
* qtscript
* qtscript
* qtsvg
* qttools
* qttranslations

 On Windows, you also need qtwinextras, on Linux qtx11extras, on OSX qtmacextras.

When installing from source, you can use these example configure commands:
 
* Linux:

    ./configure \
        -skip qt3d \
        -skip qtactiveqt \
        -skip qtcanvas3d \
        -skip qtconnectivity \
        -skip qtdoc \
        -skip qtenginio \
        -skip qtgraphicaleffects \
        -skip qtlocation \
        -skip qtmultimedia \
        -skip qtsensors \
        -skip qtserialport \
        -skip qtwayland \
        -skip qtwebchannel \
        -skip qtwebengine \
        -skip qtwebkit \
        -skip qtwebkit-examples \
        -skip qtwebsockets \
        -skip qtxmlpatterns \
        -opensource -confirm-license -release \
        -no-qml-debug -no-mtdev -no-journald \
        -no-openssl -no-libproxy \
        -no-pulseaudio -no-alsa -no-nis \
        -no-cups -no-tslib -no-pch \
        -no-dbus  -no-gstreamer -no-system-proxies \
        -no-openssl -no-libproxy -no-pulseaudio \
        -qt-xcb  -xcb -qt-freetype -qt-harfbuzz \
        -qt-pcre -qt-xkbcommon-x11 -xcb-xlib  \
        -prefix BUILDROOT/i
    make -j8

* OSX

./configure \
    -skip qt3d \
    -skip qtactiveqt \
    -skip qtcanvas3d \
    -skip qtconnectivity \
    -skip qtdoc \
    -skip qtenginio \
    -skip qtgraphicaleffects \
    -skip qtlocation \
    -skip qtmultimedia \
    -skip qtsensors \
    -skip qtserialport \
    -skip qtwayland \
    -skip qtwebchannel \
    -skip qtwebengine \
    -skip qtwebsockets \
    -skip qtxmlpatterns \
    -opensource -confirm-license -release \
    -no-qml-debug -no-mtdev -no-journald \
    -no-openssl -no-libproxy \
    -no-pulseaudio -no-alsa -no-nis \
    -no-cups -no-tslib -no-pch \
    -no-dbus  -no-gstreamer -no-system-proxies \
    -no-openssl -no-libproxy -no-pulseaudio \
    -nomake examples -nomake tests \
    -prefix $BUILDROOT/i

* Windows

    To do a normal release build use:

    configure -skip qt3d -skip qtactiveqt -skip qtcanvas3d -skip qtconnectivity -skip qtdoc -skip qtenginio -skip qtgraphicaleffects -skip qtlocation -skip qtmultimedia -skip qtsensors -skip qtserialport -skip qtwayland -skip qtwebchannel -skip qtwebengine -skip qtwebsockets -skip qtwebview -skip qtxmlpatterns -no-sql-sqlite -nomake examples -nomake tools -no-compile-examples -no-dbus -no-iconv -no-angle -no-ssl -no-openssl -no-wmf-backend -no-qml-debug -no-libproxy -no-system-proxies -no-nis -no-icu -no-mtdev -opensource -confirm-license -release -opengl desktop -qt-zlib -qt-pcre -qt-libpng -qt-libjpeg -prefix %BUILDROOT%\i

    If a release with debug info for developing purposes is needed, add the -force-debug-info to the configure options above, so that the pdb files will be generated.

    N.B.: if you don't have the Qt 5.6 pre-built binaries already
    installed, you will need to copy Qt5Core.dll, coming from those
    binaries inside the Qt build directory, under the path qtbase\bin,
    otherwise you'll get an error about it missing when using jom
    and a more cryptic error with nmake. The same has to be done with
    Qt5Xml.dll, to be copied under qttools\bin.  Don't use the dlls
    coming from the QtCreator folder, they won't work.

== Prepare the externals build ==

1. enter the BUILDROOT/b directory
2. run cmake:
    * Linux:
    cmake ../krita/3rdparty -DINSTALL_ROOT=BUILDROOT/i -DEXTERNALS_DOWNLOAD_DIR=BUILDROOT/d -DCMAKE_INSTALL_PREFIX=BUILDROOT/i
:
    * OSX:
    * Windows 32 bits:

    * Windows 64 bits:

Note that the cmake command needs to point to your BUILDROOT like /dev/d, not c:\dev\d.

    set PATH=BUILDROOT\i\bin\;BUILDROOT\i\lib;%PATH%
    cmake ..\krita\3rdparty -DEXTERNALS_DOWNLOAD_DIR=/dev/d -DINSTALL_ROOT=/dev/i   -G "Visual Studio 14 Win64" 
    

3. build the packages:

With a judicious application of DEPENDS statements, it's possible to build it all in one go, but in my experience that fails anyway, so it's better to build the dependencies indepdendently.

On Windows:

    cmake --build . --config RelWithDebInfo --target ext_patch
    cmake --build . --config RelWithDebInfo --target ext_png2ico
    cmake --build . --config RelWithDebInfo --target ext_pthreads


On all operating systems:

    cmake --build . --config RelWithDebInfo --target ext_boost
    cmake --build . --config RelWithDebInfo --target ext_eigen3
    cmake --build . --config RelWithDebInfo --target ext_exiv2
    cmake --build . --config RelWithDebInfo --target ext_fftw3
    cmake --build . --config RelWithDebInfo --target ext_ilmbase
    cmake --build . --config RelWithDebInfo --target ext_jpeg
    cmake --build . --config RelWithDebInfo --target ext_lcms2
    cmake --build . --config RelWithDebInfo --target ext_ocio
    cmake --build . --config RelWithDebInfo --target ext_openexr
    cmake --build . --config RelWithDebInfo --target ext_gsl

Note for OSX:

On OSX, you need to first build openexr; that will fail; then you need to set the rpath for the two utilities correctly, then try to build openexr again.

    install_name_tool -add_rpath $BUILD_ROOT/i/lib $BUILD_ROOT/b/ext_openexr/ext_openexr-prefix/src/ext_openexr-build/IlmImf/./b44ExpLogTable
    install_name_tool -add_rpath $BUILD_ROOT/i/lib $BUILD_ROOT/b/ext_openexr/ext_openexr-prefix/src/ext_openexr-build/IlmImf/./dwaLookups

On All operating systems:

    cmake --build . --config RelWithDebInfo --target ext_png
    cmake --build . --config RelWithDebInfo --target ext_tiff
    cmake --build . --config RelWithDebInfo --target ext_gsl
    cmake --build . --config RelWithDebInfo --target ext_vc
    cmake --build . --config RelWithDebInfo --target ext_libraw
    cmake --build . --config RelWithDebInfo --target ext_openjpeg

On Windows and OSX

    cmake --build . --config RelWithDebInfo --target ext_kwindowsystem

On Windows

(Note: skip this for now if you're using msvc 2015, poppler isn't compatible
with that compiler yet)

    cmake --build . --config RelWithDebInfo --target ext_freetype
    cmake --build . --config RelWithDebInfo --target ext_poppler

On Linux

    cmake --build . --config RelWithDebInfo --target ext_kcrash


Note: poppler should be buildable on Linux as well with a home-built freetype
and fontconfig, but I don't know how to make fontconfig find freetype, and on
Linux, fontconfig is needed for poppler. Poppler is needed for PDF import.

Note 2: libcurl still isn't available.

== Build Krita ==
 
1. Make a krita build directory: 
    mkdir BUILDROOT/build
2. Enter the BUILDROOT/build
3. Run 

On Windows
    Depending on what you want to use, run this command for MSBuild (slower compiling, since it doesn't properly use all the CPU cores, but ships with Visual Studio)

    cmake ..\krita -G "Visual Studio 14 Win64" -DBoost_DEBUG=OFF -DBOOST_INCLUDEDIR=c:\dev\i\include -DBOOST_DEBUG=ON -DBOOST_ROOT=c:\dev\i -DBOOST_LIBRARYDIR=c:\dev\i\lib -DCMAKE_INSTALL_PREFIX=c:\dev\i -DCMAKE_PREFIX_PATH=c:\dev\i -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF -DKDE4_BUILD_TESTS=OFF -DHAVE_MEMORY_LEAK_TRACKER=OFF -DPACKAGERS_BUILD=ON -Wno-dev -DDEFINE_NO_DEPRECATED=1

    Or this to later use jom (faster compiling, uses all cores, ships with QtCreator/pre-built Qt binaries):

    cmake ..\krita -G "NMake Makefiles" -DBoost_DEBUG=OFF -DBOOST_INCLUDEDIR=c:\dev\i\include -DBOOST_DEBUG=ON -DBOOST_ROOT=c:\dev\i -DBOOST_LIBRARYDIR=c:\dev\i\lib -DCMAKE_INSTALL_PREFIX=c:\dev\i -DCMAKE_PREFIX_PATH=c:\dev\i -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF -DKDE4_BUILD_TESTS=OFF -DHAVE_MEMORY_LEAK_TRACKER=OFF -DPACKAGERS_BUILD=ON -Wno-dev -DDEFINE_NO_DEPRECATED=1

On Linux

    cmake ../krita -DCMAKE_INSTALL_PREFIX=BUILDROOT/i -DDEFINE_NO_DEPRECATED=1 -DPACKAGERS_BUILD=ON -DBUILD_TESTING=OFF -DKDE4_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo

On OSX

    cmake ../krita -DCMAKE_INSTALL_PREFIX=/Users/boud/dev/i -DDEFINE_NO_DEPRECATED=1 -DBUILD_TESTING=OFF -DKDE4_BUILD_TESTS=OFF -DPACKAGERS_BUILD=ON  -DBUNDLE_INSTALL_DIR=$HOME/dev/i/bin -DCMAKE_BUILD_TYPE=RelWithDebInfo


4. Run 

On Linux and OSX
    make
    make install

On Windows
    Either use MSBuild to build:

    cmake --build . --config RelWithDebInfo --target INSTALL

    Or use jom which should be in a path similar to C:\Qt\Qt5.6.0\Tools\QtCreator\bin\jom.exe.
    So, from the same folder, instead of running cmake run:

    "C:\Qt\Qt5.6.0\Tools\QtCreator\bin\jom.exe" install

6. Run krita:

On Linux

    BUILDROOT/i/bin/krita

On Windows

    BUILDROOT\i\bin\krita.exe

On OSX

    BUILDROOT/i/bin/krita.app/Contents/MacOS/krita

== Packaging a Windows Build ==
If you want to create a stripped down version of Krita to distribute, after building everything just copy the makepkg.bat file from the "windows" folder inside krita root source folder to BUILDROOT and run it.

That will copy the Krita necessary files into the specified folder and leave behind developer related files, so the resulting folder will be a smaller install folder.

== Common Issues ==
- On Windows, if you get a 'mspdb140.dll' missing alert window, it means you did not run the bat file. Make sure to include the quotes in the command:
  "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\amd64\vcvars64.bat"

- On Windows, if you get an error about Qt5Core.dll missing/not found or nmake exit with an error that mention QT_PLUGIN_PATH, you have to copy a couple of dlls in the Qt build directory, look for the N.B. in the Qt instructions at the start of the Readme.

- If you receive an error while compiling about "missing QtCore5.cmake", or something similar, check to make sure qmake is in your PATH. Restart your command line after any changes are made.
