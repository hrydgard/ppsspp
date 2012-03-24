#include "json/json_writer.h"
#include <iostream>

int main() {
  JsonWriter j;
  j.begin();
  j.pushDict("settings");
  j.writeInt("volume", 50);
  j.writeInt("sound", 60);
  j.pop();
  j.pushDict("user");
  j.writeFloat("level", 1.5);
  j.writeString("name", "hello");
  j.pop();
  j.writeInt("outer", 3);
  j.pushArray("ints");
  j.writeInt(3);
  j.writeInt(4);
  j.writeInt(6);
  j.pop();
  j.writeString("yo!", "yo");
  j.end();
  std::cout << j.str();
  return 0;
}
