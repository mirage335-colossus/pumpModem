// DVB short-frame LDPC tables extracted without changes from xdsopl/LDPC
// commit 32357d8ad55a6a302c34e093759f0454e45cca56 (see README.md and LICENSE).
// Copyright 2018 Ahmet Inan <xdsopl@gmail.com>
#pragma once

struct DVB_S2_TABLE_C4
{
	static const int M = 360;
	static const int N = 16200;
	static const int K = 7200;
	static const int LINKS_MIN_CN = 4;
	static const int LINKS_MAX_CN = 7;
	static const int LINKS_TOTAL = 48599;
	static const int DEG_MAX = 8;
	static constexpr int DEG[] = {
		8, 3, 0
	};
	static constexpr int LEN[] = {
		5, 15, 0
	};
	static constexpr int POS[] = {
		20,	712,	2386,	6354,	4061,	1062,	5045,	5158,
		21,	2543,	5748,	4822,	2348,	3089,	6328,	5876,
		22,	926,	5701,	269,	3693,	2438,	3190,	3507,
		23,	2802,	4520,	3577,	5324,	1091,	4667,	4449,
		24,	5140,	2003,	1263,	4742,	6497,	1185,	6202,
		0,	4046,	6934,
		1,	2855,	66,
		2,	6694,	212,
		3,	3439,	1158,
		4,	3850,	4422,
		5,	5924,	290,
		6,	1467,	4049,
		7,	7820,	2242,
		8,	4606,	3080,
		9,	4633,	7877,
		10,	3884,	6868,
		11,	8935,	4996,
		12,	3028,	764,
		13,	5988,	1057,
		14,	7411,	3450,
	};
};

struct DVB_S2_TABLE_C6
{
	static const int M = 360;
	static const int N = 16200;
	static const int K = 10800;
	static const int LINKS_MIN_CN = 9;
	static const int LINKS_MAX_CN = 10;
	static const int LINKS_TOTAL = 53999;
	static const int DEG_MAX = 13;
	static constexpr int DEG[] = {
		13, 3, 0
	};
	static constexpr int LEN[] = {
		3, 27, 0
	};
	static constexpr int POS[] = {
		0,	2084,	1613,	1548,	1286,	1460,	3196,	4297,	2481,	3369,	3451,	4620,	2622,
		1,	122,	1516,	3448,	2880,	1407,	1847,	3799,	3529,	373,	971,	4358,	3108,
		2,	259,	3399,	929,	2650,	864,	3996,	3833,	107,	5287,	164,	3125,	2350,
		3,	342,	3529,
		4,	4198,	2147,
		5,	1880,	4836,
		6,	3864,	4910,
		7,	243,	1542,
		8,	3011,	1436,
		9,	2167,	2512,
		10,	4606,	1003,
		11,	2835,	705,
		12,	3426,	2365,
		13,	3848,	2474,
		14,	1360,	1743,
		0,	163,	2536,
		1,	2583,	1180,
		2,	1542,	509,
		3,	4418,	1005,
		4,	5212,	5117,
		5,	2155,	2922,
		6,	347,	2696,
		7,	226,	4296,
		8,	1560,	487,
		9,	3926,	1640,
		10,	149,	2928,
		11,	2364,	563,
		12,	635,	688,
		13,	231,	1684,
		14,	1129,	3894,
	};
};

struct DVB_S2_TABLE_C7
{
	static const int M = 360;
	static const int N = 16200;
	static const int K = 11880;
	static const int LINKS_MIN_CN = 9;
	static const int LINKS_MAX_CN = 13;
	static const int LINKS_TOTAL = 47519;
	static const int DEG_MAX = 12;
	static constexpr int DEG[] = {
		12, 3, 0
	};
	static constexpr int LEN[] = {
		1, 32, 0
	};
	static constexpr int POS[] = {
		3,	3198,	478,	4207,	1481,	1009,	2616,	1924,	3437,	554,	683,	1801,
		4,	2681,	2135,
		5,	3107,	4027,
		6,	2637,	3373,
		7,	3830,	3449,
		8,	4129,	2060,
		9,	4184,	2742,
		10,	3946,	1070,
		11,	2239,	984,
		0,	1458,	3031,
		1,	3003,	1328,
		2,	1137,	1716,
		3,	132,	3725,
		4,	1817,	638,
		5,	1774,	3447,
		6,	3632,	1257,
		7,	542,	3694,
		8,	1015,	1945,
		9,	1948,	412,
		10,	995,	2238,
		11,	4141,	1907,
		0,	2480,	3079,
		1,	3021,	1088,
		2,	713,	1379,
		3,	997,	3903,
		4,	2323,	3361,
		5,	1110,	986,
		6,	2532,	142,
		7,	1690,	2405,
		8,	1298,	1881,
		9,	615,	174,
		10,	1648,	3112,
		11,	1415,	2808,
	};
};
