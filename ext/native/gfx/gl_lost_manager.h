#pragma once

// On Android, even OpenGL can lose allocated resources. This is a utility to keep
// track of them.

// It's important to realize that with OpenGL, there's no Lost event that can be relied upon.
// The only solid indication we get is onSurfaceCreated. That is called every time the graphics
// surface that we render upon has been recreated. When that's called, we know that any
// gl resources we owned before it was called have been killed and need to be recreated.

// However, with D3D UWP, and potentially other platforms, there is a lost event.
// So we keep that infrastructure, but with GL we simply call both Lost and Restore when we detect a Restore.

// For code simplicity, it may be a good idea to manually tear down and recreate everything. Even in this case,
// it's important to use this to zero out resource handles in GLLost() - gl_lost should be called before you
// tear things down, so then you can check if handles are zero and avoid deleting resources that are already gone.

class GfxResourceHolder {
public:
	virtual ~GfxResourceHolder() {}
	virtual void GLLost() = 0;
	virtual void GLRestore() = 0;
};

void gl_lost_manager_init();
void gl_lost_manager_shutdown();

// The string pointed to by desc must be a constant or otherwise live for the entire registered lifetime of the object.
void register_gl_resource_holder(GfxResourceHolder *holder, const char *desc, int priority);
void unregister_gl_resource_holder(GfxResourceHolder *holder);

// Notifies all objects it's time to forget / delete things.
void gl_lost();

// Notifies all objects that it's time to be restored.
void gl_restore();
