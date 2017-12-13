#include "UI/OnScreenDisplay.h"

OnScreenMessages osm;

void OnScreenMessages::Show(const std::string &message, float duration_s, uint32_t color, int icon, bool checkUnique, const char *id) {
	(void)message;
   (void)duration_s;
   (void)color;
   (void)icon;
   (void)checkUnique;
   (void)id;
}

void OnScreenMessages::ShowOnOff(const std::string &message, bool b, float duration_s, uint32_t color, int icon) {
	(void)message;
   (void)b;
   (void)duration_s;
   (void)color;
   (void)icon;
}
