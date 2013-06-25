#include "input_state.h"

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
