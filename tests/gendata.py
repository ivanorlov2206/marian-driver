import sys

CH_COUNT = 128
RATE = int(sys.argv[1])
FORMAT_B = 4
TIME = int(sys.argv[2])

total_bytes = RATE * FORMAT_B * TIME
path = 'data/'

pattern = ''.join(['|' + str(x).zfill(8) for x in range(total_bytes // 9)]).encode()
pat_len = len(pattern)

for i in range(CH_COUNT):
	f = open(path + 'out.wav.{}'.format(i), 'wb')
	count = total_bytes // pat_len
	s = pattern * count + pattern[:total_bytes % pat_len]
	f.write(s)
	f.close()
