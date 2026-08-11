print("=== Clock Test ===")
local t0 = os.clock()
local sum = 0
local i = 0
while i < 100000000 do
    sum = sum + i
    i = i + 1
end
local t1 = os.clock()
print("Sum: " .. sum)
print("Elapsed: " .. (t1 - t0))
