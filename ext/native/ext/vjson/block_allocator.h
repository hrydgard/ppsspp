#ifndef BLOCK_ALLOCATOR_H
#define BLOCK_ALLOCATOR_H

class block_allocator
{
public:
	block_allocator(size_t blocksize);
	~block_allocator();

	// exchange contents with rhs
	void swap(block_allocator &rhs);

	// allocate memory
	void *malloc(size_t size);

	// free all allocated blocks
	void free();

private:
	struct block
	{
		size_t size;
		size_t used;
		char *buffer;
		block *next;
	};

	block *m_head;
	size_t m_blocksize;

	block_allocator(const block_allocator &);
	block_allocator &operator=(block_allocator &);
};

#endif
