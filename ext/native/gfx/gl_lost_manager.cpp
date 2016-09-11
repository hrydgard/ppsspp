#include <vector>

#include "base/basictypes.h"
#include "base/logging.h"
#include "gfx/gl_lost_manager.h"

std::vector<GfxResourceHolder *> *holders;

static bool inLost;
static bool inRestore;

void register_gl_resource_holder(GfxResourceHolder *holder) {
	if (inLost || inRestore) {
		FLOG("BAD: Should not call register_gl_resource_holder from lost/restore path");
		return;
	}
	if (holders) {
		holders->push_back(holder);
	} else {
		WLOG("GL resource holder not initialized, cannot register resource");
	}
}

void unregister_gl_resource_holder(GfxResourceHolder *holder) {
	if (inLost || inRestore) {
		FLOG("BAD: Should not call unregister_gl_resource_holder from lost/restore path");
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

void gl_restore() {
	inRestore = true;
	if (!holders) {
		WLOG("GL resource holder not initialized, cannot process restore request");
		inRestore = false;
		return;
	}

	ILOG("gl_restore() restoring %i items:", (int)holders->size());
	for (size_t i = 0; i < holders->size(); i++) {
		ILOG("gl_restore(%i / %i, %p, %08x)", (int)(i + 1), (int)holders->size(), (*holders)[i], *((uint32_t *)((*holders)[i])));
		(*holders)[i]->GLRestore();
	}
	ILOG("gl_restore() completed on %i items:", (int)holders->size());
	inRestore = false;
}

void gl_lost() {
	inLost = true;
	if (!holders) {
		WLOG("GL resource holder not initialized, cannot process restore request");
		inLost = false;
		return;
	}

	ILOG("gl_lost() clearing %i items:", (int)holders->size());
	for (size_t i = 0; i < holders->size(); i++) {
		ILOG("gl_lost(%i / %i, %p, %08x)", (int)(i + 1), (int)holders->size(), (*holders)[i], *((uint32_t *)((*holders)[i])));
		(*holders)[i]->GLLost();
	}
	ILOG("gl_lost() completed on %i items:", (int)holders->size());
	inLost = false;
}

void gl_lost_manager_init() {
	if (holders) {
		FLOG("Double GL lost manager init");
		// Dead here (FLOG), no need to delete holders
	}
	holders = new std::vector<GfxResourceHolder *>();
}

void gl_lost_manager_shutdown() {
	if (!holders) {
		FLOG("Lost manager already shutdown");
	} else if (holders->size() > 0) {
		ELOG("Lost manager shutdown with %i objects still registered", (int)holders->size());
	}

	delete holders;
	holders = 0;
}
