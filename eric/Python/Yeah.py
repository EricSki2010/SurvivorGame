from math import sqrt

gRatio = (sqrt(5) - 1) / 2
setX = 0
setY = 0
def functionX(x, y):
    return (3 - x)**2 + 7*(y - x**2)**2

def take_step(fn, spread=10):
    global setX, setY
    xAxisMax = setX + spread
    xAxisMin = setX - spread
    yAxisMax = setY + spread
    yAxisMin = setY - spread
    previousA = ["", 0]
    for i in range(10):
        p1 = xAxisMin + (xAxisMax - xAxisMin) * gRatio
        p2 = xAxisMin + (xAxisMax - xAxisMin) * (1 - gRatio)
        a1 = previousA[1] if previousA[0] == "a1" else fn(p1, setY)
        a2 = previousA[1] if previousA[0] == "a2" else fn(p2, setY)
        if (a1 < a2):
            xAxisMin = p2
            previousA = ["a2", a1]
        else:
            xAxisMax = p1
            previousA = ["a1", a2]
    setX = (xAxisMin + xAxisMax)/2
    previousA = ["", 0]
    for i in range(10):
        p1 = yAxisMin + (yAxisMax - yAxisMin) * gRatio
        p2 = yAxisMin + (yAxisMax - yAxisMin) * (1 - gRatio)
        a1 = previousA[1] if previousA[0] == "a1" else fn(setX, p1)
        a2 = previousA[1] if previousA[0] == "a2" else fn(setX, p2)
        if (a1 < a2):
            yAxisMin = p2
            previousA = ["a2", a1]
        else:
            yAxisMax = p1
            previousA = ["a1", a2]
    setY = (yAxisMin + yAxisMax)/2

def findMin(fn, attempts):
    for i in range(attempts):
        take_step(fn, spread=10 / (i + 3))

findMin(functionX, 10000)
print(functionX(setX, setY))
print(setX, setY)
        
