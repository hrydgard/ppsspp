#pragma once

// On Android, even OpenGL can lose allocated resources. This is a utility to keep
// track of them.

class GfxResourceHolder {
public:
	virtual ~GfxResourceHolder() {}
	virtual void GLRestore() = 0;
	virtual void GLLost() = 0;
};

void gl_lost_manager_init();
void gl_lost_manager_shutdown();

void register_gl_resource_holder(GfxResourceHolder *holder);
void unregister_gl_resource_holder(GfxResourceHolder *holder);

// Notifies all objects it's time to forget / delete things.
void gl_lost();

// Notifies all objects that it's time to be restored.
void gl_restore();
