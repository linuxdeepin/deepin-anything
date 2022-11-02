#!/usr/bin/env python

# SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

mil = 8
print 'Max Index Length: {0}'.format(mil)
print '\tName-Len\tIndex-Count\tMem-(p8)\tMem-(p4)\n'
print '=' * 80
for i in range(1, 101):
	index_count = i*(i+1)/2 if i <= mil else mil*(2*i-mil+1)/2
	print '\t{0}\t\t{1}\t\t{2}\t\t{3}'.format(i,
		index_count,
		i*(i+1)*(i+29)/6 if i <= mil else index_count*(8+1) + mil*(mil+1)*(3*i-2*mil+2)/6,
		i*(i+1)*(i+17)/6 if i <= mil else index_count*(4+1) + mil*(mil+1)*(3*i-2*mil+2)/6,
	)
