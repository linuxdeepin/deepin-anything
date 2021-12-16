/*
 * Copyright (C) 2021 UOS Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *             yangwu <yangwu@uniontech.com>
 *             wangrong <wangrong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>

#ifdef CONFIG_SW
#include <uapi/linux/ptrace.h>
#endif

unsigned long get_arg(struct pt_regs* regs, int n)
{
/*
	x86_64 args order: rdi, rsi, rdx, rcx, r8, r9, then comes the stack
	loongson3 64bit args order: a0, a1, a2, a3, which is regs[4] ~ regs[7], then comes the stack
	PT_R4(32) is the offset of regs[4] for loongson 64-bit, quoted from asm-offsets.h
*/
	switch (n) {
#if	defined(CONFIG_X86_64)

		case 1:	return regs->di;
		case 2: return regs->si;
		case 3: return regs->dx;
		case 4: return regs->cx;
		case 5: return regs->r8;
		case 6: return regs->r9;

#elif defined(CONFIG_CPU_LOONGSON3)

		case 1:  // a0
		case 2:  // a1
		case 3:  // a2
		case 4:  // a3
			return *(unsigned long*)((char *)regs + (3+n)*8);

#elif defined(CONFIG_SW)

		case 1: return regs->r16;
		case 2: return regs->r17;
		case 3: return regs->r18;
		case 4: return regs->r19;

#elif defined(CONFIG_ARM64)

		/*修改ARM上anything不能使用的问题,寄存器地址不正确*/	
		case 1: return regs->regs[0];
		case 2: return regs->regs[1];
		case 3: return regs->regs[2];
		case 4: return regs->regs[3];

#endif // CONFIG_X86_64
		default:
			return 0;
	}
	return 0;
}
