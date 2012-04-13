/* $Id: server_framebuffer.c $ */

/** @file
 * VBox OpenGL: EXT_framebuffer_object
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

#include "cr_spu.h"
#include "chromium.h"
#include "cr_mem.h"
#include "cr_net.h"
#include "server_dispatch.h"
#include "server.h"

void SERVER_DISPATCH_APIENTRY
crServerDispatchGenFramebuffersEXT(GLsizei n, GLuint *framebuffers)
{
    GLuint *local_buffers = (GLuint *) crAlloc(n * sizeof(*local_buffers));
    (void) framebuffers;
    cr_server.head_spu->dispatch_table.GenFramebuffersEXT(n, local_buffers);
    crServerReturnValue(local_buffers, n * sizeof(*local_buffers));
    crFree(local_buffers);
}

void SERVER_DISPATCH_APIENTRY
crServerDispatchGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers)
{
    GLuint *local_buffers = (GLuint *) crAlloc(n * sizeof(*local_buffers));
    (void) renderbuffers;
    cr_server.head_spu->dispatch_table.GenFramebuffersEXT(n, local_buffers);
    crServerReturnValue(local_buffers, n * sizeof(*local_buffers));
    crFree(local_buffers);
}

void SERVER_DISPATCH_APIENTRY crServerDispatchFramebufferTexture1DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    crStateFramebufferTexture1DEXT(target, attachment, textarget, texture, level);
    cr_server.head_spu->dispatch_table.FramebufferTexture1DEXT(target, attachment, textarget, crStateGetTextureHWID(texture), level);
}

void SERVER_DISPATCH_APIENTRY crServerDispatchFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    crStateFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
    cr_server.head_spu->dispatch_table.FramebufferTexture2DEXT(target, attachment, textarget, crStateGetTextureHWID(texture), level);
}

void SERVER_DISPATCH_APIENTRY crServerDispatchFramebufferTexture3DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset)
{
    crStateFramebufferTexture3DEXT(target, attachment, textarget, texture, level, zoffset);
    cr_server.head_spu->dispatch_table.FramebufferTexture3DEXT(target, attachment, textarget, crStateGetTextureHWID(texture), level, zoffset);
}

void SERVER_DISPATCH_APIENTRY crServerDispatchBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
	crStateBindFramebufferEXT(target, framebuffer);

    if (0==framebuffer && crServerIsRedirectedToFBO())
    {
        cr_server.head_spu->dispatch_table.BindFramebufferEXT(target, cr_server.curClient->currentMural->idFBO);
    }
    else
    {
        cr_server.head_spu->dispatch_table.BindFramebufferEXT(target, crStateGetFramebufferHWID(framebuffer));
    }
}

void SERVER_DISPATCH_APIENTRY crServerDispatchBindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
	crStateBindRenderbufferEXT(target, renderbuffer);
	cr_server.head_spu->dispatch_table.BindRenderbufferEXT(target, crStateGetRenderbufferHWID(renderbuffer));
}

void SERVER_DISPATCH_APIENTRY crServerDispatchDeleteFramebuffersEXT(GLsizei n, const GLuint * framebuffers)
{
	crStateDeleteFramebuffersEXT(n, framebuffers);
}

void SERVER_DISPATCH_APIENTRY crServerDispatchDeleteRenderbuffersEXT(GLsizei n, const GLuint * renderbuffers)
{
	crStateDeleteRenderbuffersEXT(n, renderbuffers);
}

void SERVER_DISPATCH_APIENTRY
crServerDispatchFramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
	crStateFramebufferRenderbufferEXT(target, attachment, renderbuffertarget, renderbuffer);
	cr_server.head_spu->dispatch_table.FramebufferRenderbufferEXT(target, attachment, renderbuffertarget, crStateGetRenderbufferHWID(renderbuffer));
}

void SERVER_DISPATCH_APIENTRY
crServerDispatchGetFramebufferAttachmentParameterivEXT(GLenum target, GLenum attachment, GLenum pname, GLint * params)
{
	GLint local_params[1];
	(void) params;
	crStateGetFramebufferAttachmentParameterivEXT(target, attachment, pname, local_params);

	crServerReturnValue(&(local_params[0]), 1*sizeof(GLint));
}

GLboolean SERVER_DISPATCH_APIENTRY crServerDispatchIsFramebufferEXT( GLuint framebuffer )
{
    GLboolean retval;
    retval = cr_server.head_spu->dispatch_table.IsFramebufferEXT(crStateGetFramebufferHWID(framebuffer));
    crServerReturnValue( &retval, sizeof(retval) );
    return retval; /* WILL PROBABLY BE IGNORED */
}

GLboolean SERVER_DISPATCH_APIENTRY crServerDispatchIsRenderbufferEXT( GLuint renderbuffer )
{
    GLboolean retval;
    retval = cr_server.head_spu->dispatch_table.IsRenderbufferEXT(crStateGetRenderbufferHWID(renderbuffer));
    crServerReturnValue( &retval, sizeof(retval) );
    return retval; /* WILL PROBABLY BE IGNORED */
}
