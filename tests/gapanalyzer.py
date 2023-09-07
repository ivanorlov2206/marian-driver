f = open("data/rec.wav.0", "rb")
data = f.read().decode()

i = 0
arr = []
while i < len(data):
	if data[i] == '|':
		nums = data[i+1:i+9].rstrip('\x00')
		if len(nums) == 8 and nums.isdigit():
			arr.append(int(nums))
		else:
			print(nums)
		i += len(nums)
	i += 1
s = -1
if len(arr) == 0:
	print("There is no numbers in the file :(");
	exit(0)
if arr[0] != 0:
	arr = [0] + arr
for i in range(1, len(arr)):
	if arr[i] - arr[i - 1] != 1:
		print("There is a gap between {} and {}. Numbers missing: {}".format(arr[i - 1], arr[i], arr[i] - arr[i - 1]))

f.close()
