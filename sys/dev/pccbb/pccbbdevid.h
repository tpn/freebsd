/*
 * Copyright (c) 2001 M. Warner Losh. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* Vendor/Device IDs */
#define	PCI_DEVICE_ID_PCIC_CLPD6729	0x11001013ul	/* 16bit I/O */
#define	PCI_DEVICE_ID_PCIC_CLPD6832	0x11101013ul
#define	PCI_DEVICE_ID_PCIC_CLPD6833	0x11131013ul
#define	PCI_DEVICE_ID_PCIC_CLPD6834	0x11121013ul
#define	PCI_DEVICE_ID_PCIC_OZ6729	0x67291217ul
#define	PCI_DEVICE_ID_PCIC_OZ6730	0x673A1217ul
#define	PCI_DEVICE_ID_PCIC_OZ6832	0x68321217ul	/* Also 6833 */
#define	PCI_DEVICE_ID_PCIC_OZ6860	0x68361217ul	/* Also 6836 */
#define	PCI_DEVICE_ID_PCIC_OZ6872	0x68721217ul	/* Also 6812 */
#define	PCI_DEVICE_ID_PCIC_OZ6912	0x69721217ul	/* Also 6972? */
#define	PCI_DEVICE_ID_PCIC_OZ6922	0x69251217ul
#define	PCI_DEVICE_ID_PCIC_OZ6933	0x69331217ul
#define	PCI_DEVICE_ID_RICOH_RL5C465	0x04651180ul
#define	PCI_DEVICE_ID_RICOH_RL5C466	0x04661180ul
#define	PCI_DEVICE_ID_RICOH_RL5C475	0x04751180ul
#define	PCI_DEVICE_ID_RICOH_RL5C476	0x04761180ul
#define	PCI_DEVICE_ID_RICOH_RL5C477	0x04771180ul
#define	PCI_DEVICE_ID_RICOH_RL5C478	0x04781180ul
#define	PCI_DEVICE_ID_OMEGA_82C094	0x1221119bul	/* 16bit I/O */
#define	PCI_DEVICE_ID_PCIC_TI1031	0xac13104cul
#define	PCI_DEVICE_ID_PCIC_TI1130	0xac12104cul
#define	PCI_DEVICE_ID_PCIC_TI1131	0xac15104cul
#define	PCI_DEVICE_ID_PCIC_TI1210	0xac1a104cul
#define	PCI_DEVICE_ID_PCIC_TI1211	0xac1e104cul
#define	PCI_DEVICE_ID_PCIC_TI1220	0xac17104cul
#define	PCI_DEVICE_ID_PCIC_TI1221	0xac19104cul
#define	PCI_DEVICE_ID_PCIC_TI1225	0xac1c104cul
#define	PCI_DEVICE_ID_PCIC_TI1250	0xac16104cul
#define	PCI_DEVICE_ID_PCIC_TI1260	0xac18104cul
#define	PCI_DEVICE_ID_PCIC_TI1260B	0xac30104cul
#define	PCI_DEVICE_ID_PCIC_TI1251	0xac1d104cul
#define	PCI_DEVICE_ID_PCIC_TI1251B	0xac1f104cul
#define	PCI_DEVICE_ID_PCIC_TI1410	0xac50104cul
#define	PCI_DEVICE_ID_PCIC_TI1420	0xac51104cul
#define	PCI_DEVICE_ID_PCIC_TI1421	0xac53104cul
#define	PCI_DEVICE_ID_PCIC_TI1450	0xac1b104cul
#define	PCI_DEVICE_ID_PCIC_TI1451	0xac52104cul
#define	PCI_DEVICE_ID_PCIC_TI4410	0xac41104cul
#define	PCI_DEVICE_ID_PCIC_TI4450	0xac40104cul
#define	PCI_DEVICE_ID_PCIC_TI4451	0xac42104cul
#define	PCI_DEVICE_ID_TOSHIBA_TOPIC95	0x06031179ul
#define	PCI_DEVICE_ID_TOSHIBA_TOPIC95B	0x060a1179ul
#define	PCI_DEVICE_ID_TOSHIBA_TOPIC97	0x060f1179ul
#define	PCI_DEVICE_ID_TOSHIBA_TOPIC100	0x06171179ul

/*
 * Other ID, from sources too vague to be reliable
 *	Mfg		  model		PCI ID
 *   Intel		82092AA		0x12218086	16bit I/O
 *   smc/Databook	DB87144		0x310610b3
 *   SMC/databook	smc34c90	0xb10610b3
 *   Omega/Trident	82c194		0x01941023
 *   Omega/Trident	82c722		0x07221023?
 *   Opti		82c814		0xc8141045
 *   Opti		82c824		0xc8241045
 *   NEC		uPD66369	0x003e1033
 */
