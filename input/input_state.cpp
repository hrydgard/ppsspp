#include "input_state.h"
#include <iostream>

#define max_queue MAX_KEYQUEUESIZE

void MakeRoomInQueueFront(int queue[])
{
	// shift queue elements from front towards back
	for (int i = max_queue - 1; i > 0; i--) {
		queue[i] = queue[i - 1];
	}
	queue[0] = 0;
}

void FillGap(int queue[], int gap_loc)
{
	// shift elements from back towards front
	// until gap location is filled
	for (int i = gap_loc; i < max_queue; i++) {
		queue[i] = queue[i + 1];
	}
	queue[max_queue - 1] = 0;
}

void KeyQueueAddKey(int queue[], int key)
{
	for (int i = 0; i < max_queue; i++) {
		if (queue[i] == key)
			return;
		if (queue[i] == 0) {
			queue[i] = key;
			return;
		}
	}
	
	// queue was full
	MakeRoomInQueueFront(queue);
	queue[0] = key;
}

void KeyQueueRemoveKey(int queue[], int key)
{
	for (int i = 0; i < max_queue; i++)
		if (queue[i] == key)
			FillGap(queue, i);
}

void KeyQueueCopyQueue(int src[], int dst[])
{
	for (int i = 0; i < max_queue; i++)
		dst[i] = src[i];
}
void KeyQueueBlank(int queue[])
{
	for (int i = 0; i < max_queue; i++)
		queue[i] = 0;
}

int AttemptTranslate(const std::map<int, int> trans_table, int plat_key)
{
	std::map<int, int>::const_iterator iter;
	iter = trans_table.find(plat_key);
	if (iter == trans_table.end())
		return 0;

	return iter->second;
}


void KeyQueueAttemptTranslatedAdd(int queue[],
                                  const std::map<int, int> translation_table, 
                                  int platform_key)
{
	int key = AttemptTranslate(translation_table, platform_key);

	if (key == 0) {
		std::cerr << "Warning: Platform key code translation table missing "
		          << "(" << platform_key << ")\n";
		return;
	}

	KeyQueueAddKey(queue, key);
}


void KeyQueueAttemptTranslatedRemove(int queue[],
                                     const std::map<int, int> translation_table, 
                                     int platform_key)
{
	int key = AttemptTranslate(translation_table, platform_key);

	if (key == 0)
		return;

	KeyQueueRemoveKey(queue, key);
}
