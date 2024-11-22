#pragma once

#include "Common/Log.h"
#include "Common/Data/Collections/Slice.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

// Queue with a dynamic size, optimized for bulk inserts and retrievals - and optimized
// to be fast in debug builds, hence it's pretty much C internally.
class CharQueue {
public:
	explicit CharQueue(size_t blockSize = 16384) : blockSize_(blockSize) {
		head_ = new Block{};
		tail_ = head_;
		head_->data = (char *)malloc(blockSize_);
		head_->size = (int)blockSize_;
		head_->next = nullptr;
	}

	// Remove copy constructors.
	CharQueue(const CharQueue &) = delete;
	void operator=(const CharQueue &) = delete;

	// But let's have a move constructor.
	CharQueue(CharQueue &&src) noexcept {
		// Steal the data from the other queue.
		blockSize_ = src.blockSize_;
		head_ = src.head_;
		tail_ = src.tail_;
		// Give the old queue a new block. Could probably also leave it in an invalid state and get rid of it.
		src.head_ = new Block{};
		src.tail_ = src.head_;
		src.head_->data = (char *)malloc(src.blockSize_);
		src.head_->size = (int)src.blockSize_;
	}

	~CharQueue() {
		clear();
		_dbg_assert_(head_ == tail_);
		_dbg_assert_(head_->size == blockSize_);
		_dbg_assert_(size() == 0);
		// delete the final block
		delete head_;
	}

	char *push_back_write(size_t size) {
		int remain = tail_->size - tail_->tail;
		_dbg_assert_(remain >= 0);
		if (remain >= (int)size) {
			char *retval = tail_->data + tail_->tail;
			tail_->tail += (int)size;
			return retval;
		} else {
			// Can't fit? Just allocate a new block and fill it up with the new data.
			int bsize = (int)blockSize_;
			if (size > bsize) {
				bsize = (int)size;
			}
			Block *b = new Block{};
			b->head = 0;
			b->tail = (int)size;
			b->size = bsize;
			b->data = (char *)malloc(bsize);
			tail_->next = b;
			tail_ = b;
			return tail_->data;
		}
	}

	void push_back(const char *data, size_t size) {
		memcpy(push_back_write(size), data, size);
	}

	void push_back(std::string_view chars) {
		memcpy(push_back_write(chars.size()), chars.data(), chars.size());
	}

	// For debugging, mainly.
	size_t block_count() const {
		int count = 0;
		Block *b = head_;
		do {
			count++;
			b = b->next;
		} while (b);
		return count;
	}

	size_t size() const {
		size_t s = 0;
		Block *b = head_;
		do {
			s += b->tail - b->head;
			b = b->next;
		} while (b);
		return s;
	}

	char peek(size_t peekOff) {
		Block *b = head_;
		do {
			int remain = b->tail - b->head;
			if (remain > peekOff) {
				return b->data[b->head + peekOff];
			} else {
				peekOff -= remain;
			}
			b = b->next;
		} while (b);
		// Ran out of data.
		_dbg_assert_(false);
		return 0;
	}

	bool empty() const {
		return size() == 0;
	}

	// Pass in a lambda that takes each partial buffer as char*, size_t.
	template<typename Func>
	bool iterate_blocks(Func callback) const {
		Block *b = head_;
		do {
			if (b->tail > b->head) {
				if (!callback(b->data + b->head, b->tail - b->head)) {
					return false;
				}
			}
			b = b->next;
		} while (b);
		return true;
	}

	size_t pop_front_bulk(char *dest, size_t size) {
		int popSize = (int)size;
		int writeOff = 0;
		while (popSize > 0) {
			int remain = head_->tail - head_->head;
			int readSize = popSize;
			if (readSize > remain) {
				readSize = remain;
			}
			if (dest) {
				memcpy(dest + writeOff, head_->data + head_->head, readSize);
			}
			writeOff += readSize;
			head_->head += readSize;
			popSize -= readSize;
			if (head_->head == head_->tail) {
				// Ran out of data in this block. Let's hope there's more...
				if (head_ == tail_) {
					// Can't read any more, bail.
					break;
				}
				Block *next = head_->next;
				delete head_;
				head_ = next;
			}
		}
		return (int)size - popSize;
	}

	size_t skip(size_t size) {
		return pop_front_bulk(nullptr, size);
	}

	void clear() {
		Block *b = head_;
		// Delete all blocks except the last.
		while (b != tail_) {
			Block *next = b->next;
			delete b;
			b = next;
		}
		if (b->size != blockSize_) {
			// Restore the remaining block to default size.
			free(b->data);
			b->data = (char *)malloc(blockSize_);
			b->size = (int)blockSize_;
		}
		b->head = 0;
		b->tail = 0;
		// head and tail are now equal.
		head_ = b;
	}

	// If return value is negative, one wasn't found.
	int next_crlf_offset() {
		int offset = 0;
		Block *b = head_;
		do {
			int remain = b->tail - b->head;
			for (int i = 0; i < remain; i++) {
				if (b->data[b->head + i] == '\r') {
					// Use peek to avoid handling edge cases.
					if (peek(offset + i + 1) == '\n') {
						return offset + i;
					}
				}
			}
			offset += remain;
			b = b->next;
		} while (b);
		// Ran out of data.
		return -1;
	}

private:
	struct Block {
		~Block() {
			if (data) {
				free(data);
				data = 0;
			}
			size = 0;
		}
		Block *next;
		char *data;
		int size;  // Can be bigger than the default block size if a push is very large.
		// Internal head and tail inside the block.
		int head;
		int tail;
	};

	// There's always at least one block, initialized in the constructor.
	Block *head_;
	Block *tail_;
	// Default min block size for new blocks.
	size_t blockSize_;
};
