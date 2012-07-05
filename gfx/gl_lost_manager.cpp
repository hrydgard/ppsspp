#include <list>

#include "base/logging.h"
#include "gfx/gl_lost_manager.h"

std::list<GfxResourceHolder *> *holders;

GfxResourceHolder::~GfxResourceHolder() {}

void register_gl_resource_holder(GfxResourceHolder *holder) {
  if (holders) {
    holders->push_back(holder);
  } else {
    WLOG("GL resource holder not initialized, cannot register resource");
  }
}
void unregister_gl_resource_holder(GfxResourceHolder *holder) {
  if (holders) {
    holders->remove(holder);
  } else {
    WLOG("GL resource holder not initialized, cannot unregister resource");
  }
}

void gl_lost() {
  if (!holders) {
    WLOG("GL resource holder not initialized, cannot process lost request");
    return;
  }
  for (std::list<GfxResourceHolder *>::iterator iter = holders->begin();
       iter != holders->end(); ++iter) {
    (*iter)->GLLost();
  }
}

void gl_lost_manager_init() {
  if (holders) {
    FLOG("Double GL lost manager init");
  }
  holders = new std::list<GfxResourceHolder *>();
}

void gl_lost_manager_shutdown() {
  if (!holders) {
    FLOG("Lost manager already shutdown");
  }
  delete holders;
  holders = 0;
}
