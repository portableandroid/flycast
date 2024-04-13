/*
    Created on: Oct 19, 2019

	Copyright 2019 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "gl_context.h"

#ifndef LIBRETRO
#include "rend/gles/opengl_driver.h"
#endif

#include <cstring>

void GLGraphicsContext::findGLVersion()
{
	while (true)
		if (glGetError() == GL_NO_ERROR)
			break;
	glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
	if (glGetError() == GL_INVALID_ENUM)
		majorVersion = 2;
	else
	{
		glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
	}
	const char *version = (const char *)glGetString(GL_VERSION);
	_isGLES = !strncmp(version, "OpenGL ES", 9);
	INFO_LOG(RENDERER, "OpenGL version: %s", version);

	const char *p = (const char *)glGetString(GL_RENDERER);
	driverName = p != nullptr ? p : "unknown";
	p = (const char *)glGetString(GL_VERSION);
	driverVersion = p != nullptr ? p : "unknown";
	p = (const char *)glGetString(GL_VENDOR);
	std::string vendor = p != nullptr ? p : "";
	if (vendor.substr(0, 4) == "ATI ")
		amd = true;
	else if (driverName.find(" ATI ") != std::string::npos
			|| driverName.find(" AMD ") != std::string::npos)
		// mesa
		amd = true;
	else
		amd = false;
}

void GLGraphicsContext::postInit()
{
	instance = this;
	findGLVersion();
	resetUIDriver();
}

void GLGraphicsContext::preTerm()
{
#ifndef LIBRETRO
	imguiDriver.reset();
#endif
	instance = nullptr;
}

void GLGraphicsContext::resetUIDriver()
{
#ifndef LIBRETRO
	imguiDriver.reset();
	imguiDriver = std::unique_ptr<ImGuiDriver>(new OpenGLDriver());
#endif
}
