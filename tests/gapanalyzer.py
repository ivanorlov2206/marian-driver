def find_gaps(filenum):
	f = open("data/rec.wav.{}".format(filenum), "rb")
	data = f.read()

	i = 0
	arr = []
	while i < len(data):
		if data[i] == ord('|'):
			try:
				nums = data[i+1:i+9].decode('ascii')
				if len(nums) == 8 and nums.isdigit():
					arr.append(int(nums))
			except:
				pass
			#else:
				#print(nums)
		i += 1
	s = -1
	if len(arr) == 0:
		print("There is no numbers in the file :(");
		exit(0)
	#print(arr)
	#if arr[0] != 0:
		#arr = [0] + arr
	gaps_cnt = 0
	gs = 0
	for i in range(1, len(arr)):
		if arr[i] - arr[i - 1] != 1:
			print("There is a gap between {} and {}. Numbers missing: {}".format(arr[i - 1], arr[i], arr[i] - arr[i - 1]))
			gaps_cnt += 1
			gs += (arr[i] - arr[i - 1] - 1)
	f.close()
	return gaps_cnt, gs

for i in range(1):
	gaps, ts = find_gaps(i)
	print("There is", gaps, "gaps in the", i, "file with total", ts, 'nums skipped');
