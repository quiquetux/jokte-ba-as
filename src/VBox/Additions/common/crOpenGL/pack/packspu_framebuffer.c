/* $Id: packspu_framebuffer.c $ */

/** @file
 * VBox OpenGL FBO related functions
 */

/*
 * Copyright (C) 2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "packspu.h"
#include "cr_packfunctions.h"
#include "cr_net.h"
#include "packspu_proto.h"

void PACKSPU_APIENTRY
packspu_FramebufferTexture1DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    crStateFramebufferTexture1DEXT(target, attachment, textarget, texture, level);
    crPackFramebufferTexture1DEXT(target, attachment, textarget, texture, level);
}

void PACKSPU_APIENTRY
packspu_FramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    crStateFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
    crPackFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
}

void PACKSPU_APIENTRY
packspu_FramebufferTexture3DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset)
{
    crStateFramebufferTexture3DEXT(target, attachment, textarget, texture, level, zoffset);
    crPackFramebufferTexture3DEXT(target, attachment, textarget, texture, level, zoffset);
}

void PACKSPU_APIENTRY
packspu_BindFramebufferEXT(GLenum target, GLuint framebuffer)
{
	crStateBindFramebufferEXT(target, framebuffer);
    crPackBindFramebufferEXT(target, framebuffer);
}

void PACKSPU_APIENTRY
packspu_DeleteFramebuffersEXT(GLsizei n, const GLuint * framebuffers)
{
	crStateDeleteFramebuffersEXT(n, framebuffers);
    crPackDeleteFramebuffersEXT(n, framebuffers);
}

void PACKSPU_APIENTRY
packspu_DeleteRenderbuffersEXT(GLsizei n, const GLuint * renderbuffers)
{
	crStateDeleteFramebuffersEXT(n, renderbuffers);
    crPackDeleteFramebuffersEXT(n, renderbuffers);
}

void PACKSPU_APIENTRY
packspu_FramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
	crStateFramebufferRenderbufferEXT(target, attachment, renderbuffertarget, renderbuffer);
    crPackFramebufferRenderbufferEXT(target, attachment, renderbuffertarget, renderbuffer);
}

void PACKSPU_APIENTRY
packspu_BindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
    crStateBindRenderbufferEXT(target, renderbuffer);
    crPackBindRenderbufferEXT(target, renderbuffer);
}

GLenum PACKSPU_APIENTRY
packspu_CheckFramebufferStatusEXT(GLenum target)
{
	GET_THREAD(thread);
	int writeback = 1;
    GLenum status = crStateCheckFramebufferStatusEXT(target);

    if (status!=GL_FRAMEBUFFER_UNDEFINED)
    {
        return status;
    }

    crPackCheckFramebufferStatusEXT(target, &status, &writeback);

	packspuFlush((void *) thread);
	while (writeback)
		crNetRecv();

    crStateSetFramebufferStatus(target, status);
    return status;
}
