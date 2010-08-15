#ifndef LATIN1_TO_GSM_H
#define LATIN1_TO_GSM_H

static const int latin1_to_gsm[256] = {
/* 0x00 */ NRP, /* pc: NON PRINTABLE */
/* 0x01 */ NRP, /* pc: NON PRINTABLE */
/* 0x02 */ NRP, /* pc: NON PRINTABLE */
/* 0x03 */ NRP, /* pc: NON PRINTABLE */
/* 0x04 */ NRP, /* pc: NON PRINTABLE */
/* 0x05 */ NRP, /* pc: NON PRINTABLE */
/* 0x06 */ NRP, /* pc: NON PRINTABLE */
/* 0x07 */ NRP, /* pc: NON PRINTABLE */
/* 0x08 */ NRP, /* pc: NON PRINTABLE */
/* 0x09 */ NRP, /* pc: NON PRINTABLE */
/* 0x0a */ 0x0a, /* pc: NON PRINTABLE */
/* 0x0b */ NRP, /* pc: NON PRINTABLE */
/* 0x0c */ -0x0a, /* pc: NON PRINTABLE */
/* 0x0d */ 0x0d, /* pc: NON PRINTABLE */
/* 0x0e */ NRP, /* pc: NON PRINTABLE */
/* 0x0f */ NRP, /* pc: NON PRINTABLE */
/* 0x10 */ NRP, /* pc: NON PRINTABLE */
/* 0x11 */ NRP, /* pc: NON PRINTABLE */
/* 0x12 */ NRP, /* pc: NON PRINTABLE */
/* 0x13 */ NRP, /* pc: NON PRINTABLE */
/* 0x14 */ NRP, /* pc: NON PRINTABLE */
/* 0x15 */ NRP, /* pc: NON PRINTABLE */
/* 0x16 */ NRP, /* pc: NON PRINTABLE */
/* 0x17 */ NRP, /* pc: NON PRINTABLE */
/* 0x18 */ NRP, /* pc: NON PRINTABLE */
/* 0x19 */ NRP, /* pc: NON PRINTABLE */
/* 0x1a */ NRP, /* pc: NON PRINTABLE */
/* 0x1b */ NRP, /* pc: NON PRINTABLE */
/* 0x1c */ NRP, /* pc: NON PRINTABLE */
/* 0x1d */ NRP, /* pc: NON PRINTABLE */
/* 0x1e */ NRP, /* pc: NON PRINTABLE */
/* 0x1f */ NRP, /* pc: NON PRINTABLE */
/* 0x20 */ 0x20, /* pc:   */
/* 0x21 */ 0x21, /* pc: ! */
/* 0x22 */ 0x22, /* pc: " */
/* 0x23 */ 0x23, /* pc: # */
/* 0x24 */ 0x02, /* pc: $ */
/* 0x25 */ 0x25, /* pc: % */
/* 0x26 */ 0x26, /* pc: & */
/* 0x27 */ 0x27, /* pc: ' */
/* 0x28 */ 0x28, /* pc: ( */
/* 0x29 */ 0x29, /* pc: ) */
/* 0x2a */ 0x2a, /* pc: * */
/* 0x2b */ 0x2b, /* pc: + */
/* 0x2c */ 0x2c, /* pc: , */
/* 0x2d */ 0x2d, /* pc: - */
/* 0x2e */ 0x2e, /* pc: . */
/* 0x2f */ 0x2f, /* pc: / */
/* 0x30 */ 0x30, /* pc: 0 */
/* 0x31 */ 0x31, /* pc: 1 */
/* 0x32 */ 0x32, /* pc: 2 */
/* 0x33 */ 0x33, /* pc: 3 */
/* 0x34 */ 0x34, /* pc: 4 */
/* 0x35 */ 0x35, /* pc: 5 */
/* 0x36 */ 0x36, /* pc: 6 */
/* 0x37 */ 0x37, /* pc: 7 */
/* 0x38 */ 0x38, /* pc: 8 */
/* 0x39 */ 0x39, /* pc: 9 */
/* 0x3a */ 0x3a, /* pc: : */
/* 0x3b */ 0x3b, /* pc: ; */
/* 0x3c */ 0x3c, /* pc: < */
/* 0x3d */ 0x3d, /* pc: = */
/* 0x3e */ 0x3e, /* pc: > */
/* 0x3f */ 0x3f, /* pc: ? */
/* 0x40 */ 0x00, /* pc: @ */
/* 0x41 */ 0x41, /* pc: A */
/* 0x42 */ 0x42, /* pc: B */
/* 0x43 */ 0x43, /* pc: C */
/* 0x44 */ 0x44, /* pc: D */
/* 0x45 */ 0x45, /* pc: E */
/* 0x46 */ 0x46, /* pc: F */
/* 0x47 */ 0x47, /* pc: G */
/* 0x48 */ 0x48, /* pc: H */
/* 0x49 */ 0x49, /* pc: I */
/* 0x4a */ 0x4a, /* pc: J */
/* 0x4b */ 0x4b, /* pc: K */
/* 0x4c */ 0x4c, /* pc: L */
/* 0x4d */ 0x4d, /* pc: M */
/* 0x4e */ 0x4e, /* pc: N */
/* 0x4f */ 0x4f, /* pc: O */
/* 0x50 */ 0x50, /* pc: P */
/* 0x51 */ 0x51, /* pc: Q */
/* 0x52 */ 0x52, /* pc: R */
/* 0x53 */ 0x53, /* pc: S */
/* 0x54 */ 0x54, /* pc: T */
/* 0x55 */ 0x55, /* pc: U */
/* 0x56 */ 0x56, /* pc: V */
/* 0x57 */ 0x57, /* pc: W */
/* 0x58 */ 0x58, /* pc: X */
/* 0x59 */ 0x59, /* pc: Y */
/* 0x5a */ 0x5a, /* pc: Z */
/* 0x5b */ -0x3c, /* pc: [ */
/* 0x5c */ -0x2f, /* pc: \ */
/* 0x5d */ -0x3e, /* pc: ] */
/* 0x5e */ -0x14, /* pc: ^ */
/* 0x5f */ 0x11, /* pc: _ */
/* 0x60 */ NRP, /* pc: ` */
/* 0x61 */ 0x61, /* pc: a */
/* 0x62 */ 0x62, /* pc: b */
/* 0x63 */ 0x63, /* pc: c */
/* 0x64 */ 0x64, /* pc: d */
/* 0x65 */ 0x65, /* pc: e */
/* 0x66 */ 0x66, /* pc: f */
/* 0x67 */ 0x67, /* pc: g */
/* 0x68 */ 0x68, /* pc: h */
/* 0x69 */ 0x69, /* pc: i */
/* 0x6a */ 0x6a, /* pc: j */
/* 0x6b */ 0x6b, /* pc: k */
/* 0x6c */ 0x6c, /* pc: l */
/* 0x6d */ 0x6d, /* pc: m */
/* 0x6e */ 0x6e, /* pc: n */
/* 0x6f */ 0x6f, /* pc: o */
/* 0x70 */ 0x70, /* pc: p */
/* 0x71 */ 0x71, /* pc: q */
/* 0x72 */ 0x72, /* pc: r */
/* 0x73 */ 0x73, /* pc: s */
/* 0x74 */ 0x74, /* pc: t */
/* 0x75 */ 0x75, /* pc: u */
/* 0x76 */ 0x76, /* pc: v */
/* 0x77 */ 0x77, /* pc: w */
/* 0x78 */ 0x78, /* pc: x */
/* 0x79 */ 0x79, /* pc: y */
/* 0x7a */ 0x7a, /* pc: z */
/* 0x7b */ -0x28, /* pc: { */
/* 0x7c */ -0x40, /* pc: | */
/* 0x7d */ -0x29, /* pc: } */
/* 0x7e */ -0x3d, /* pc: ~ */
/* 0x7f */ NRP, /* pc: NON PRINTABLE */
/* 0x80 */ NRP, /* pc: NON PRINTABLE */
/* 0x81 */ NRP, /* pc: NON PRINTABLE */
/* 0x82 */ NRP, /* pc: NON PRINTABLE */
/* 0x83 */ NRP, /* pc: NON PRINTABLE */
/* 0x84 */ NRP, /* pc: NON PRINTABLE */
/* 0x85 */ NRP, /* pc: NON PRINTABLE */
/* 0x86 */ NRP, /* pc: NON PRINTABLE */
/* 0x87 */ NRP, /* pc: NON PRINTABLE */
/* 0x88 */ NRP, /* pc: NON PRINTABLE */
/* 0x89 */ NRP, /* pc: NON PRINTABLE */
/* 0x8a */ NRP, /* pc: NON PRINTABLE */
/* 0x8b */ NRP, /* pc: NON PRINTABLE */
/* 0x8c */ NRP, /* pc: NON PRINTABLE */
/* 0x8d */ NRP, /* pc: NON PRINTABLE */
/* 0x8e */ NRP, /* pc: NON PRINTABLE */
/* 0x8f */ NRP, /* pc: NON PRINTABLE */
/* 0x90 */ NRP, /* pc: NON PRINTABLE */
/* 0x91 */ NRP, /* pc: NON PRINTABLE */
/* 0x92 */ NRP, /* pc: NON PRINTABLE */
/* 0x93 */ NRP, /* pc: NON PRINTABLE */
/* 0x94 */ NRP, /* pc: NON PRINTABLE */
/* 0x95 */ NRP, /* pc: NON PRINTABLE */
/* 0x96 */ NRP, /* pc: NON PRINTABLE */
/* 0x97 */ NRP, /* pc: NON PRINTABLE */
/* 0x98 */ NRP, /* pc: NON PRINTABLE */
/* 0x99 */ NRP, /* pc: NON PRINTABLE */
/* 0x9a */ NRP, /* pc: NON PRINTABLE */
/* 0x9b */ NRP, /* pc: NON PRINTABLE */
/* 0x9c */ NRP, /* pc: NON PRINTABLE */
/* 0x9d */ NRP, /* pc: NON PRINTABLE */
/* 0x9e */ NRP, /* pc: NON PRINTABLE */
/* 0x9f */ NRP, /* pc: NON PRINTABLE */
/* 0xa0 */ NRP, /* pc: NON PRINTABLE */
/* 0xa1 */ 0x40, /* pc: INVERTED EXCLAMATION MARK */
/* 0xa2 */ NRP, /* pc: NON PRINTABLE */
/* 0xa3 */ 0x01, /* pc: POUND SIGN */
/* 0xa4 */ 0x24, /* pc: CURRENCY SIGN */
/* 0xa5 */ 0x03, /* pc: YEN SIGN*/
/* 0xa6 */ NRP, /* pc: NON PRINTABLE */
/* 0xa7 */ 0x5f, /* pc: SECTION SIGN */
/* 0xa8 */ NRP, /* pc: NON PRINTABLE */
/* 0xa9 */ NRP, /* pc: NON PRINTABLE */
/* 0xaa */ NRP, /* pc: NON PRINTABLE */
/* 0xab */ NRP, /* pc: NON PRINTABLE */
/* 0xac */ NRP, /* pc: NON PRINTABLE */
/* 0xad */ NRP, /* pc: NON PRINTABLE */
/* 0xae */ NRP, /* pc: NON PRINTABLE */
/* 0xaf */ NRP, /* pc: NON PRINTABLE */
/* 0xb0 */ NRP, /* pc: NON PRINTABLE */
/* 0xb1 */ NRP, /* pc: NON PRINTABLE */
/* 0xb2 */ NRP, /* pc: NON PRINTABLE */
/* 0xb3 */ NRP, /* pc: NON PRINTABLE */
/* 0xb4 */ NRP, /* pc: NON PRINTABLE */
/* 0xb5 */ NRP, /* pc: NON PRINTABLE */
/* 0xb6 */ NRP, /* pc: NON PRINTABLE */
/* 0xb7 */ NRP, /* pc: NON PRINTABLE */
/* 0xb8 */ NRP, /* pc: NON PRINTABLE */
/* 0xb9 */ NRP, /* pc: NON PRINTABLE */
/* 0xba */ NRP, /* pc: NON PRINTABLE */
/* 0xbb */ NRP, /* pc: NON PRINTABLE */
/* 0xbc */ NRP, /* pc: NON PRINTABLE */
/* 0xbd */ NRP, /* pc: NON PRINTABLE */
/* 0xbe */ NRP, /* pc: NON PRINTABLE */
/* 0xbf */ 0x60, /* pc: INVERTED QUESTION MARK */
/* 0xc0 */ NRP, /* pc: NON PRINTABLE */
/* 0xc1 */ NRP, /* pc: NON PRINTABLE */
/* 0xc2 */ NRP, /* pc: NON PRINTABLE */
/* 0xc3 */ NRP, /* pc: NON PRINTABLE */
/* 0xc4 */ 0x5b, /* pc: LATIN CAPITAL LETTER A WITH DIAERESIS */
/* 0xc5 */ 0x0e, /* pc: LATIN CAPITAL LETTER A WITH RING ABOVE */
/* 0xc6 */ 0x1c, /* pc: LATIN CAPITAL LETTER AE */
/* 0xc7 */ 0x09, /* pc: LATIN CAPITAL LETTER C WITH CEDILLA (mapped to small) */
/* 0xc8 */ NRP, /* pc: NON PRINTABLE */
/* 0xc9 */ 0x1f, /* pc: LATIN CAPITAL LETTER E WITH ACUTE  */
/* 0xca */ NRP, /* pc: NON PRINTABLE */
/* 0xcb */ NRP, /* pc: NON PRINTABLE */
/* 0xcc */ NRP, /* pc: NON PRINTABLE */
/* 0xcd */ NRP, /* pc: NON PRINTABLE */
/* 0xce */ NRP, /* pc: NON PRINTABLE */
/* 0xcf */ NRP, /* pc: NON PRINTABLE */
/* 0xd0 */ NRP, /* pc: NON PRINTABLE */
/* 0xd1 */ 0x5d, /* pc: LATIN CAPITAL LETTER N WITH TILDE */
/* 0xd2 */ NRP, /* pc: NON PRINTABLE */
/* 0xd3 */ NRP, /* pc: NON PRINTABLE */
/* 0xd4 */ NRP, /* pc: NON PRINTABLE */
/* 0xd5 */ NRP, /* pc: NON PRINTABLE */
/* 0xd6 */ 0x5c, /* pc: LATIN CAPITAL LETTER O WITH DIAEREIS */
/* 0xd7 */ NRP, /* pc: NON PRINTABLE */
/* 0xd8 */ 0x0b, /* pc: LATIN CAPITAL LETTER O WITH STROKE */
/* 0xd9 */ NRP, /* pc: NON PRINTABLE */
/* 0xda */ NRP, /* pc: NON PRINTABLE */
/* 0xdb */ NRP, /* pc: NON PRINTABLE */
/* 0xdc */ 0x5e, /* pc: LATIN CAPITAL LETTER U WITH DIAERESIS */
/* 0xdd */ NRP, /* pc: NON PRINTABLE */
/* 0xde */ NRP, /* pc: NON PRINTABLE */
/* 0xdf */ 0x1e, /* pc: LATIN SMALL LETTER SHARP S */
/* 0xe0 */ 0x7f, /* pc: LATIN SMALL LETTER A WITH GRAVE */
/* 0xe1 */ NRP, /* pc: NON PRINTABLE */
/* 0xe2 */ NRP, /* pc: NON PRINTABLE */
/* 0xe3 */ NRP, /* pc: NON PRINTABLE */
/* 0xe4 */ 0x7b, /* pc: LATIN SMALL LETTER A WITH DIAERESIS */
/* 0xe5 */ 0x0f, /* pc: LATIN SMALL LETTER A WITH RING ABOVE */
/* 0xe6 */ 0x1d, /* pc: LATIN SMALL LETTER AE */
/* 0xe7 */ 0x09, /* pc: LATIN SMALL LETTER C WITH CEDILLA */
/* 0xe8 */ 0x04, /* pc: NON PRINTABLE */
/* 0xe9 */ 0x05, /* pc: NON PRINTABLE */
/* 0xea */ NRP, /* pc: NON PRINTABLE */
/* 0xeb */ NRP, /* pc: NON PRINTABLE */
/* 0xec */ 0x07, /* pc: NON PRINTABLE */
/* 0xed */ NRP, /* pc: NON PRINTABLE */
/* 0xee */ NRP, /* pc: NON PRINTABLE */
/* 0xef */ NRP, /* pc: NON PRINTABLE */
/* 0xf0 */ NRP, /* pc: NON PRINTABLE */
/* 0xf1 */ 0x7d, /* pc: NON PRINTABLE */
/* 0xf2 */ 0x08, /* pc: NON PRINTABLE */
/* 0xf3 */ NRP, /* pc: NON PRINTABLE */
/* 0xf4 */ NRP, /* pc: NON PRINTABLE */
/* 0xf5 */ NRP, /* pc: NON PRINTABLE */
/* 0xf6 */ 0x7c, /* pc: NON PRINTABLE */
/* 0xf7 */ NRP, /* pc: NON PRINTABLE */
/* 0xf8 */ 0x0c, /* pc: NON PRINTABLE */
/* 0xf9 */ 0x06, /* pc: NON PRINTABLE */
/* 0xfa */ NRP, /* pc: NON PRINTABLE */
/* 0xfb */ NRP, /* pc: NON PRINTABLE */
/* 0xfc */ 0x7e, /* pc: NON PRINTABLE */
/* 0xfd */ NRP, /* pc: NON PRINTABLE */
/* 0xfe */ NRP, /* pc: NON PRINTABLE */
/* 0xff */ NRP, /* pc: NON PRINTABLE */
};

#endif
