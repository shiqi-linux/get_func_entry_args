/*
 * qian can <qiancan31863@gmail.com>
 */


#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/signal.h>
#include <asm/stacktrace.h>
#include <asm/traps.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>

struct do_execveat_common_list
{
    struct list_head list;
    char   filename[32];
};

static char *symbol_name = NULL;
module_param(symbol_name, charp, 0644);

static unsigned int offset = 0x00;
module_param(offset, uint, 0644);

static struct kprobe kp = {.symbol_name = "?"};

struct do_execveat_common_list g_do_execveat_common_list;


#ifdef CONFIG_ARM
static void show_backtrace(struct pt_regs *regs)
{
    struct stackframe frame;
    int urc;
    unsigned long where;
    
    frame.fp = regs->ARM_fp;
    frame.sp = regs->ARM_sp;
    frame.lr = regs->ARM_lr;
    /* use lr as frame.pc, because use pc cant dumpstack, maybe in kprobes */
    frame.pc = regs->ARM_lr;
        
    while(1)
    {
        where = frame.pc;
        urc = unwind_frame(&frame);
        if (urc < 0)
        {
            break;
        }

        dump_backtrace_entry(where, frame.pc, frame.sp-4);
    }    
}
#endif

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    unsigned long clone_flags = 0;
    unsigned long stack_start = 0;
    struct filename *filename = NULL;
    struct do_execveat_common_list *p_do_execveat_common;
    struct list_head *pos;
   
    int sig = 0;
    struct siginfo *info = NULL;
    struct task_struct *t = NULL;

    p_do_execveat_common = kmalloc(sizeof(struct do_execveat_common_list), GFP_KERNEL);
    memset(p_do_execveat_common, 0x00, sizeof(struct do_execveat_common_list));
    printk("----------------hander_pre-------------------\n");
    printk("comm=%s pid=%d\n", current->comm, current->pid);
#ifdef CONFIG_ARM
    printk("pc is at %pS\n", (void*)instruction_pointer(regs));
    printk("lr is at %pS\n", (void*)regs->ARM_lr);

    printk("pc : [%08lx] lr : [%08lx]\n", regs->ARM_pc, regs->ARM_lr);
    printk("sp : %08lx\n", regs->ARM_sp);
    printk("r0 : %08lx r1 : %08lx r2: %08lx\n", regs->ARM_r0, regs->ARM_r1, regs->ARM_r2 );

    if (strncmp(p->symbol_name, "_do_fork", 8) == 0)
    {
        clone_flags = (unsigned long)regs->ARM_r0;
        stack_start = (unsigned long)regs->ARM_r1;
        if (clone_flags & CLONE_UNTRACED)
        {
            printk("[_do_fork] kernel_thread \n");
        }
        else if (clone_flags & CLONE_VFORK)
        {
            printk("[_do_fork] vfork \n");
        }
        else if (clone_flags == SIGCHLD)
        {
            printk("[_do_fork] fork \n");
        }
        else
        {
            printk("[_do_fork] clone \n");
        }
    }
    else if (strncmp(p->symbol_name, "do_execveat_common", 18) == 0)
    {
        filename = (struct filename *)regs->ARM_r1;
        if (filename)
        {
            printk("[do_execveat_common] filename : %s \n", filename->name);
            strncpy(p_do_execveat_common->filename, filename->name, strlen(filename->name));
            p_do_execveat_common->filename[strlen(filename->name)] = '\0';

            list_add_tail(&p_do_execveat_common->list,&g_do_execveat_common_list.list);
        }
    }
    else if (strncmp(p->symbol_name, "__send_signal", 13) == 0)
    {
        sig = (int)regs->ARM_r0;
        info = (struct siginfo *)regs->ARM_r1;
        t = (struct task_struct *)regs->ARM_r2;

        printk("[%s-%d] send signal %d to [%s-%d] \n", current->comm, current->pid, sig, t->comm, t->pid);
    }
    else
    {
        printk("symbol_name: %s\n", p->symbol_name);
    }
    
    //list_for_each(pos, &g_do_execveat_common_list.list)
    //{
    //    printk("filename %s \n", ((struct do_execveat_common_list*)pos)->filename);
    //}
    show_backtrace(regs);
#endif
    return 0;
}


static void handler_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    return;
}


static int handler_fault(struct kprobe *p, struct pt_regs *regs, int trapnr)
{
    return 0;
}


static void *qtool_seq_start(struct seq_file *s, loff_t *pos)
{
    static unsigned long counter = 0;

    if (*pos == 0)
    {
        return &counter;
    }
    else
    {
        *pos = 0;
        return NULL;
    }
}


static void *qtool_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    unsigned long *tmp_v = (unsigned long *)v;
    (*tmp_v)++;
    (*pos)++;
    return NULL;
}


static void qtool_seq_stop(struct seq_file *s, void *v)
{

}


static int qtool_seq_show(struct seq_file *s, void *v)
{
    unsigned long len = 0;
    struct list_head *tmp;

    list_for_each(tmp, &g_do_execveat_common_list.list)
    {
        //len += snprintf(s+len, 32, "filename %s \n", ((struct do_execveat_common_list*)tmp)->filename);
        seq_printf(s, "filename %s\n", ((struct do_execveat_common_list*)tmp)->filename);
    }
    
    //seq_printf(s, "%d\n", *a);
    return 0;
}


static struct seq_operations qtool_seq_ops = {
    .start = qtool_seq_start,
    .next = qtool_seq_next,
    .stop = qtool_seq_stop,
    .show = qtool_seq_show
};


static int qtool_seq_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &qtool_seq_ops);
}

static const struct file_operations qtool_proc_ops = {
    .owner = THIS_MODULE,
    .open = qtool_seq_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};


static void qtool_proc_create(void)
{
    struct proc_dir_entry *qtool_entry = NULL;

    qtool_entry = proc_create("qtool", 0x644, NULL, &qtool_proc_ops);

    if (!qtool_entry)
    {
        printk("create qtool proc fail. \n");
    }
}


static void qtool_proc_remove(void)
{
    remove_proc_entry("qtool", NULL);
}


int __init qtool_init(void)
{
    int ret = 0;
    if (!symbol_name)
    {
        printk("symbol_name null \n");
        return -1;
    }

    kp.symbol_name = symbol_name;
    kp.offset = offset;
    kp.pre_handler = handler_pre;
    kp.post_handler = handler_post;
    kp.fault_handler = handler_fault;

    ret = register_kprobe(&kp);
    if (ret < 0)
    {
        printk("register_kprobe failed, return %d\n" ,ret);
        return ret;
    }

    if (strncmp(symbol_name, "do_execveat_common", 18) == 0)
    {
        INIT_LIST_HEAD(&g_do_execveat_common_list.list);
    }

    qtool_proc_create();
    return 0;
}


void __exit qtool_exit(void)
{
    unregister_kprobe(&kp);
    qtool_proc_remove();
}


module_init(qtool_init);
module_exit(qtool_exit);
MODULE_LICENSE("GPL");
