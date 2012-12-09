/******************************************************************/
/* scheduler.c - The scheduler and PIT controller for the kernel. */
/******************************************************************/
#include "scheduler.h"
#include "lib.h"
#include "i8259.h"
#include "syscalls.h"


/*
 * pit_init()
 *
 * Description:
 * Initializes the PIT.
 *
 * Inputs: none
 * Retvals: none
 */
void pit_init(void) 
{
	/* Set frequency to 33HZ (interrupt every 30 milliseconds). */
    outb(PIT_MODE3, PIT_CMDREG);
    outb(DIVISOR_33HZ & 0xFF, PIT_CHANNEL0);
    outb(DIVISOR_33HZ >> 8, PIT_CHANNEL0);

	/* Output from PIT channel 0 is connected to the PIC chip, so that it 
	 * generates an "IRQ 0" */
	enable_irq(PIT_IRQ);
}

/*
 * pit_interruption()
 *
 * Description:
 * The handler for an PIT interrupt. Invokes process scheduling action.
 *
 * Inputs: none
 * Retvals: none
 */
void pit_interruption(void)
{
	/* Mask interrupts */
	cli();

	/* ????? */
	change_process();
	/* ????? */
	
	/* Send EOI, otherwise we freeze up. */
	send_eoi(PIT_IRQ);

	/* Unmask interrupts */
	sti();
}

/* ?????????????????????????????????????????????????????????????????????????????
 * change_process()
 *
 * Description:
 * Here is what I imagine we do to get this scheduler to work.
 *  - (What's already happened: the PIT is a timer which will fire occasionally
 *     when it's time to move on to the next process in the round-robin
 *     scheduling technique. The PIT fires and is set up in the idt to link to a
 * 	   handy interrupt wrapper which calls "pit_interruption" which calls this.)
 *  - We need to switch to the next available process now.
 *  - To do that we do what we do in a halt syscall, but instead of dropping
 *    back into the parent process, we want to simply move onto the next process
 *    in our "running_processes" bitmask.
 *  --- All this should take is swapping the current ksp and ebp with the
 *      next_ksp and next_ebp (instead of the parent_ksp and parent_ebp).
 *  - PITFALL: We can't simply move onto the next bit in the bitmask (from bit 7
 *    to bit 6) because if bit 6 represents a shell which is currently running
 *    fish, it will fuck up the terminating of fish or do something weird-ish.
 * Sound good? 
 *  -rob
 * 
 * Inputs: ???
 * Retvals: ???
 * ?????????????????????????????????????????????????????????????????????????????
 */
void change_process()
{
	/* Local variables */
	int i;
	pcb_t * process_control_block;
	
	uint8_t running_processes = get_running_processes();
	uint8_t current_process_number = get_current_process_number();
	
	
	/* Find the next process to be scheduled */
	uint8_t next_process_number = (current_process_number+1) % 8;
	uint8_t bitmask = 0x80;
	for( i = 0; i < next_process_number; i++ )
	{
		bitmask >>= 1;
	}
	while( next_process_number != current_process_number )
	{
		/* Check to see if the process number is running (but don't look at 
		 * process #0, aka "no process running") 
		 */
		if( bitmask & (running_processes & 0x7F) )
		{
			/* Extract the PCB from the process number */
			process_control_block = (pcb_t *)( _8MB - (_8KB)*(next_process_number + 1) );
			
			if( !process_control_block->has_child )
			{
				break;
			}
		}
		
		next_process_number = (next_process_number+1) % 8;
	}
	
	/* If we didn't find another process to schedule, return */
	if( current_process_number == next_process_number )
	{
		return;
	}
	
	/* Extract the PCB from the process number */
	process_control_block = (pcb_t *)( _8MB - (_8KB)*(current_process_number + 1) );
		
	/* Store the %ESP as "ksp_before_change" in the PCB of the current process. */
	uint32_t esp;
	asm volatile("movl %%esp, %0":"=g"(esp));
	process_control_block->ksp_before_change = esp;
	
	/* Store the %EBP as "kbp_before_change" in the PCB of the current process. */
	uint32_t ebp;
	asm volatile("movl %%ebp, %0":"=g"(ebp));
	process_control_block->kbp_before_change = ebp;
	
	
	/* Set the current process to the next process */
	current_process_number = next_process_number;
	
	
	/* Load the page directory of the next process. */
	set_page_dir_addr( (uint32_t)(&page_directories[next_process_number]) );
	asm (
	"movl page_dir_addr, %%eax        ;"
	"andl $0xFFFFFFE7, %%eax          ;"
	"movl %%eax, %%cr3                ;"
	"movl %%cr4, %%eax                ;"
	"orl $0x00000090, %%eax           ;"
	"movl %%eax, %%cr4                ;"
	"movl %%cr0, %%eax                ;"
	"orl $0x80000000, %%eax 	      ;"
	"movl %%eax, %%cr0                 "
	: : : "eax", "cc" );
	
	
	/* Set the kernel_stack_bottom and the TSS to point to the next process's kernel stack. */
	tss.esp0 = _8MB - (_8KB)*next_process_number - 4;
	set_kernel_stack_bottom( _8MB - (_8KB)*next_process_number - 4 );
	
	
	/* Extract the PCB from the process number */
	process_control_block = (pcb_t *)( _8MB - (_8KB)*(next_process_number + 1) );
	
	/* Put the "ksp_before_change" of the next process into the %ESP. */
	asm volatile("movl %0, %%esp	;"
				 ::"g"(process_control_block->ksp_before_change));
				 
	/* Put the "kbp_before_change" of the next process into the %EBP. */
	asm volatile("movl %0, %%ebp"::"g"(process_control_block->kbp_before_change));
	
	/* 
	 * We have now switched to the stack of the next process (now-current process). Remember that this
	 * stack was where we originally called 'change_process' for this process. Thus, the current stack 
	 * contains the appropriate data that was pushed when we made the 'change_process' syscall for the 
	 * now-current process. If we now leave and ret, we will return back to the syscall_handler for the 
	 * original 'change_process' syscall, which can then iret, resuming the now-current process.
	 */
	asm volatile("leave");
	asm volatile("ret");
}