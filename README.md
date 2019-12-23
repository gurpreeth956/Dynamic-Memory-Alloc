# Dynamic-Memory-Allocator

This was done for an assignment for CSE320 at Stony Brook University. There is just one commit here because all the commits while working on the assignment was done in GitLab.

# About

Created a malloc, free, and realloc function in C on Linux
- Each allocated block has a header, footer, and padding for memory alignment
- Used segregated free lists to hold all free blocks to easily find them for allocating 
- Reallocating a block to a smaller block splits that block if there are no splinters 
- If possible, free blocks are coalesced to help prevent external fragmentation
