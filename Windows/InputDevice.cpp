#include "InputDevice.h"
#include "XinputDevice.h"
#include "KeyboardDevice.h"
#include <list>
#include <memory>

#define PUSH_BACK(Cls) do { list.push_back(std::shared_ptr<InputDevice>(new Cls())); } while (0)
std::list<std::shared_ptr<InputDevice>> getInputDevices() {
	std::list<std::shared_ptr<InputDevice>> list;
	PUSH_BACK(XinputDevice);
	PUSH_BACK(KeyboardDevice);
	return list;
}
