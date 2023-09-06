CH_COUNT = 128
RATE = 48000
FORMAT_B = 4
TIME = 10

total_bytes = RATE * FORMAT_B * TIME
pattern = ''.join(['|' + str(x).zfill(8) for x in range(total_bytes // 9)]).encode()
pat_len = len(pattern)
path = 'data/'

for i in range(CH_COUNT):
	f = open(path + 'out.wav.{}'.format(i), 'wb')
	count = total_bytes // pat_len
	s = pattern * count + pattern[:total_bytes % pat_len]
	f.write(s)
	f.close()
