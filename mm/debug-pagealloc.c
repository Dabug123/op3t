#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/page-debug-flags.h>
#include <linux/poison.h>
#include <linux/ratelimit.h>

#ifndef mark_addr_rdonly
#define mark_addr_rdonly(a)
#endif

#ifndef mark_addr_rdwrite
#define mark_addr_rdwrite(a)
#endif

static inline void set_page_poison(struct page *page)
{
	__set_bit(PAGE_DEBUG_FLAG_POISON, &page->debug_flags);
}

static inline void clear_page_poison(struct page *page)
{
	__clear_bit(PAGE_DEBUG_FLAG_POISON, &page->debug_flags);
}

static inline bool page_poison(struct page *page)
{
	return test_bit(PAGE_DEBUG_FLAG_POISON, &page->debug_flags);
}

static void poison_page(struct page *page)
{
	void *addr = kmap_atomic(page);

	set_page_poison(page);
	memset(addr, PAGE_POISON, PAGE_SIZE);
	mark_addr_rdonly(addr);
	kunmap_atomic(addr);
}

static void poison_pages(struct page *page, int n)
{
	int i;

	for (i = 0; i < n; i++)
		poison_page(page + i);
}

static bool single_bit_flip(unsigned char a, unsigned char b)
{
	unsigned char error = a ^ b;

	return error && !(error & (error - 1));
}
struct page_poison_information {
	u64 virtualaddress;
	u64 physicaladdress;
	char buf;
};
#define MAX_PAGE_POISON_COUNT 8
static struct page_poison_information page_poison_array[MAX_PAGE_POISON_COUNT]={{0,0}};
static int  page_poison_count = 0;
static void check_poison_mem(unsigned char *mem, size_t bytes)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 10);
	unsigned char *start;
	unsigned char *end;

	start = memchr_inv(mem, PAGE_POISON, bytes);
	if (!start)
		return;

	for (end = mem + bytes - 1; end > start; end--) {
		if (*end != PAGE_POISON)
			break;
	}

	if (!__ratelimit(&ratelimit))
		return;
	else if (start == end && single_bit_flip(*start, PAGE_POISON)){
		printk(KERN_ERR "pagealloc: single bit error\n");
		printk(KERN_ERR "virt: %p, phys: 0x%llx\n", start, virt_to_phys(start));
		if(page_poison_count == MAX_PAGE_POISON_COUNT-1){
			BUG_ON(PANIC_CORRUPTION);
			dump_stack();
		}else {
			page_poison_array[page_poison_count].virtualaddress = (u64)start;
			page_poison_array[page_poison_count].physicaladdress = (u64)(virt_to_phys(start));
			page_poison_array[page_poison_count].buf = *start;
			page_poison_count++;
		}
	}
	else{
		printk(KERN_ERR "pagealloc: memory corruption\n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 16, 1, start,
			end - start + 1, 1);
		BUG_ON(PANIC_CORRUPTION);
		dump_stack();
	}

}

static void unpoison_page(struct page *page)
{
	void *addr;

	if (!page_poison(page))
		return;

	addr = kmap_atomic(page);
	check_poison_mem(addr, PAGE_SIZE);
	mark_addr_rdwrite(addr);
	clear_page_poison(page);
	kunmap_atomic(addr);
}

static void unpoison_pages(struct page *page, int n)
{
	int i;

	for (i = 0; i < n; i++)
		unpoison_page(page + i);
}

void kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (enable)
		unpoison_pages(page, numpages);
	else
		poison_pages(page, numpages);
}
