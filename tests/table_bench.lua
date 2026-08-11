local N = 100000000
local t0 = os.clock()

local t = {}
local i = 1

while i <= N do
    t[i] = i * 2
    i = i + 1
end

local sum = 0
i = 1

while i <= N do
    sum = sum + t[i]
    i = i + 1
end

local t1 = os.clock()

print("Sum: " .. sum)
print("Elapsed: " .. (t1 - t0))
