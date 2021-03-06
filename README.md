# pyb -- Profile Your Beam

Copyright 2015--2020 Clemens Schäfermeier, clemens ( at ) fh-muenster.de

    This file is part of pyb.

    pyb is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    pyb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pyb.  If not, see <http://www.gnu.org/licenses/>.

pyb is an open source project for profiling laser beams with CCD or CMOS cameras. It includes
  - live parameter estimation
  - a 3D viewer for live visualization of the input
  - image manipulation and data export
  - a graphical user interface
  - access to IDS (R) cameras
  - Linux and Windows support

It was written in a modular way, such that other cameras can be implemented along the existing module.

Projects and libraries used:
  - OpenCV for linear algebra and data structuring
  - freeglut for visualization in OpenGL
  - wxWidgets for the GUI
  - mingw-std-threads for C++11 thread support on Windows (in case of Win32 threads)
  - OpenMP for multi core data processing in fit routines
  - gnuplot (if available on the machine) for plotting image captures

At the moment, only the Windows part has been extensively tested.
