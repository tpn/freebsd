/*	$NetBSD: if_tun.h,v 1.5 1994/06/29 06:36:27 cgd Exp $	*/

/*
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 *
 * This source may be freely distributed, however I would be interested
 * in any changes that are made.
 *
 * This driver takes packets off the IP i/f and hands them up to a
 * user process to have it's wicked way with. This driver has it's
 * roots in a similar driver written by Phil Cockcroft (formerly) at
 * UCL. This driver is based much more on read/write/select mode of
 * operation though.
 *
 * : $Header: if_tnreg.h,v 1.1.2.1 1992/07/16 22:39:16 friedl Exp
 */

#ifndef _NET_IF_TUN_H_
#define _NET_IF_TUN_H_

/* Refer to if_tunvar.h for the softc stuff */

/* Maximum transmit packet size (default) */
#define	TUNMTU		1500

/* Maximum receive packet size (hard limit) */
#define	TUNMRU		16384

struct tuninfo {
	int	baudrate;		/* linespeed */
	short	mtu;			/* maximum transmission unit */
	u_char	type;			/* ethernet, tokenring, etc. */
	u_char	dummy;			/* place holder */
};

/* ioctl's for get/set debug */
#define	TUNSDEBUG	_IOW('t', 90, int)
#define	TUNGDEBUG	_IOR('t', 89, int)
#define	TUNSIFINFO	_IOW('t', 91, struct tuninfo)
#define	TUNGIFINFO	_IOR('t', 92, struct tuninfo)

#endif /* !_NET_IF_TUN_H_ */
