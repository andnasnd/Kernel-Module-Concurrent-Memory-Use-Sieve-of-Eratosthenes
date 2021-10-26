#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>

#define RUNNING 0
#define DONE 1
#define NEXTSTEP 2 //used in Sieve function in crossing-out non-primes 
#define PERLINE 8

//module parameters
static unsigned long num_threads = 1;
module_param(num_threads, ulong, 0);

//(n-1) by array indexing
static unsigned long upper_bound = 10;
module_param(upper_bound, ulong, 0);

//array of counters 
static int *counters;
//array of integers 
static atomic_t *nums; 
//position of current number 
static int pos = 2;

//timestamp variables
static unsigned long long ts_init = 0;
static unsigned long long ts_1 = 0;
static unsigned long long ts_2 = 0;

//state tracking variables, for barrier function
atomic_t isDone; 
atomic_t b_1;
atomic_t b_2;

//locks used in barrier synchronization and prime computation
DEFINE_SPINLOCK(lock1);
DEFINE_SPINLOCK(lock2);
DEFINE_MUTEX(cur_lock); 

static int run(void *counter); 
static void sieve(unsigned long *counter);
static void barrier_1(void);
static void barrier_2(void); 

unsigned long long 
get_time (void)
{
    struct timespec ts; 
    getnstimeofday(&ts);
    return (unsigned long long)timespec_to_ns(&ts);
}

static int 
run (void *counter)
{
	barrier_1();
	sieve((unsigned long *) counter);
	barrier_2();
	atomic_set(&isDone, DONE);
	return 0;
}

//barrier functions to synchronize threads 
static void 
barrier_1 (void)
{
    spin_lock(&lock1);
	atomic_add(1, &b_1);
	if(atomic_read(&b_1) == num_threads)
    {
        printk(KERN_INFO "barrier_1 reached\n");
        ts_1 = get_time();
    }
    spin_unlock(&lock1);
	return;
}

static void 
barrier_2 (void)
{
    spin_lock(&lock2);
	atomic_add(1, &b_2);
	if(atomic_read(&b_2) == num_threads)
    {
        printk(KERN_INFO "barrier_2 reached\n");
        ts_2 = get_time();
    }
    spin_unlock(&lock2);
	return;
}

static void 
sieve (unsigned long *counter) 
{
    int i; 
    int cur_pos;  
	while(1) 
    {
      mutex_lock(&cur_lock); 
      cur_pos = pos; 
      pos++;
      while(atomic_read(&nums[pos]) == 0 &&
              cur_pos <= upper_bound)
      {
        pos++;

      }
      mutex_unlock(&cur_lock);
	  if(pos >= upper_bound-1)
      {
        break;
      }
        //crossing out multiples of primes
		for(i=cur_pos * NEXTSTEP;i<=upper_bound;i+=cur_pos)
        {
			atomic_set(&nums[i],0);
			(*counter)++;
		}
        schedule();
	}
}

static void 
print_stats (void)
{
  int i; 
  int num_primes = 0; 
  int num_nonprimes = 0;
  int total_cross = 0;
  int excess_cross = 0; 
  long long unsigned int d1; 
  long long unsigned int d2; 

  for (i=0; i < upper_bound-1; i++)
  {
      if(atomic_read(&nums[i]) != 0)
      {
          printk(KERN_CONT "%d ", i);
          num_primes++;
          if (num_primes % PERLINE == 0)
          {
              printk(KERN_CONT "\n");
          }
      }
  }

  printk(KERN_CONT "\n");
  num_nonprimes = upper_bound - 1 - num_primes; 
  for (i = 0; i < num_threads; i++)
  {
      total_cross = total_cross + counters[i];
  }
  excess_cross = total_cross - num_nonprimes; 
  //module initialization time = barrier_1 time - init time 
  d1 = ts_1 - ts_init;
  //module runtime = sieve computation time = barrier_2 time - barrier_1 time
  d2 = ts_2 - ts_1;
  printk(KERN_INFO "Module Initialization time = %llu\n", d1);
  printk(KERN_INFO "Module Runtime = %llu\n", d2);
  printk(KERN_INFO "num_threads = %lu\n", num_threads);
  printk(KERN_INFO "upper_bound = %lu\n", upper_bound);
  printk(KERN_INFO "Number of primes = %d\n", num_primes);
  printk(KERN_INFO "Number of non-primes = %d\n", num_nonprimes);
  printk(KERN_INFO "Number of excess crosses = %d\n", excess_cross);
}

static void 
free_mem (void)
{
    kfree(nums);
    kfree(counters);
}

static int 
simple_init (void) 
{
	int i;
    int j;
	ts_init = get_time();
    printk(KERN_ALERT "Initializing module\n");
    
    //safe initialization
    counters = 0;
    nums = 0; 
    atomic_set(&isDone, DONE);
    atomic_set(&b_1, RUNNING);
    atomic_set(&b_2, RUNNING);

	if (num_threads < 1)
    {
		printk(KERN_ERR "err - simple_init() num_threads > 1\n");
		counters = 0;
		nums = 0;
		upper_bound = 0;
		num_threads = 1;
		atomic_set(&isDone, DONE);
		return -1;
	}
    else if (upper_bound < 2)
    {
		printk(KERN_ERR "err - simple_init() upper_bound > 2\n");
		counters = 0;
		nums = 0;
		upper_bound = 0;
		num_threads = 0;
		atomic_set(&isDone, DONE);
		return -2;
	}

	nums = kmalloc((upper_bound-1)*sizeof(int),GFP_KERNEL);
	if(nums == NULL) {
		printk(KERN_ERR "err - integer array allocation failure\n");
        nums = 0;
        counters = 0;
        upper_bound = 0;
        num_threads = 0;
		return -3;
	}

	counters = kmalloc(num_threads*sizeof(int),GFP_KERNEL);
	if (counters == NULL) {
		printk(KERN_ERR "kmalloc failed for counters\n");
		kfree(nums);
        counters = 0;
        nums = 0; 
        upper_bound = 0;
        num_threads = 0;
		return -4;
	}
    //initialize counters for each thread
	for(i = 0; i < num_threads; i++) 
    {
		counters[i] = 0;
	}
    //initialize array of integers 
	for(i = 0; i < upper_bound-1; i++) 
    {
		atomic_set(&nums[i],i);
	}
    //initialize atomic var
	atomic_set(&isDone, RUNNING);
	
    //run threads
	for(j = 0; j < num_threads; j++) {
		kthread_run(run, counters+j, "kthread%d",j);
	}

    printk(KERN_ALERT "initialize kernel module success\n");
	return 0;
}

static void
simple_exit (void) 
{
	printk(KERN_INFO "Unloading module\n");
	if (atomic_read(&isDone) != DONE) 
    {
		printk(KERN_INFO "simple_exit() computation not complete\n");
        return;
	}	
	if (!counters) 
    {
		printk(KERN_ERR "err - simple_exit() counter array allocation failure\n");
        kfree(nums);
        return;
	}	   
	if (!nums) 
    {
		printk(KERN_ERR "err - simple_exit() integer array allocation failure\n");
        return;
	}	 
	print_stats();
	free_mem();
    printk(KERN_INFO "unload kernel module success\n");
    return;
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anand Nambakam");
MODULE_DESCRIPTION("Lab 2");
