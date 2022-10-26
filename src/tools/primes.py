#!/usr/bin/env python

# SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

def is_prime(primes, n):
	for p in primes:
		if n % p == 0:
			return False
		elif p*p > n:
			return True

def append_next_prime(primes):
	np = primes[-1] + 2
	while not is_prime(primes, np):
		np = np + 2
	primes.append(np)
	return primes

pns = [2, 3, 5, 7, 11, 13, 17, 19]
while pns[-1] < (1<<17):
	append_next_prime(pns)

print len(pns), pns[-2:]
