#include <vector>

#include "base/basictypes.h"
#include "base/logging.h"
#include "gfx/gl_lost_manager.h"

std::vector<GfxResourceHolder *> *holders;

static bool inLost;

void register_gl_resource_holder(GfxResourceHolder *holder) {
	if (inLost) {
		FLOG("BAD: Should not call register_gl_resource_holder from lost path");
		return;
	}
	if (holders) {
		holders->push_back(holder);
	} else {
		WLOG("GL resource holder not initialized, cannot register resource");
	}
}
void unregister_gl_resource_holder(GfxResourceHolder *holder) {
	if (inLost) {
		FLOG("BAD: Should not call unregister_gl_resource_holder from lost path");
		return;
	}
	if (holders) {
		for (size_t i = 0; i < holders->size(); i++) {
			if ((*holders)[i] == holder) {
				holders->erase(holders->begin() + i);
				return;
			}
		}
		WLOG("unregister_gl_resource_holder: Resource not registered");
	} else {
		WLOG("GL resource holder not initialized or already shutdown, cannot unregister resource");
	}
}

void gl_lost() {
	inLost = true;
	if (!holders) {
		WLOG("GL resource holder not initialized, cannot process lost request");
		return;
	}
	ILOG("gl_lost() restoring %i items:", (int)holders->size());
	for (size_t i = 0; i < holders->size(); i++) {
		ILOG("GLLost(%i / %i, %p)", (int) i, (int) holders->size(), (*holders)[i]);
		(*holders)[i]->GLLost();
	}
	ILOG("gl_lost() completed restoring %i items:", (int)holders->size());
	inLost = false;
}

void gl_lost_manager_init() {
	if (holders) {
		FLOG("Double GL lost manager init");
	}
	holders = new std::vector<GfxResourceHolder *>();
}

void gl_lost_manager_shutdown() {
	if (!holders) {
		FLOG("Lost manager already shutdown");
	}
	delete holders;
	holders = 0;
}
