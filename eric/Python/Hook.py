import json
from math import sqrt

points = []
gRatio = (sqrt(5) - 1) / 2
setX = 0
setY = 0

# Define function as a string so it can be shared with graph.html
# Uses syntax valid in both Python and JavaScript
functionExpr = "(3 - x)**2 + 7*(y - x**2)**2"

def functionX(x, y):
    return eval(functionExpr)

def golden_search(low, high, evaluate):
    previousA = ["", 0]
    for i in range(10):
        p1 = low + (high - low) * gRatio
        p2 = low + (high - low) * (1 - gRatio)
        a1 = previousA[1] if previousA[0] == "a1" else evaluate(p1)
        a2 = previousA[1] if previousA[0] == "a2" else evaluate(p2)
        if (a1 < a2):
            low = p2
            previousA = ["a2", a1]
        else:
            high = p1
            previousA = ["a1", a2]
    return (low + high) / 2

def search_direction(fn, dx, dy, spread):
    global setX, setY
    t = golden_search(-spread, spread, lambda t: fn(setX + t*dx, setY + t*dy))
    setX += t * dx
    setY += t * dy

def goHook(fn, spread=10):
    PreviousX = setX
    PreviousY = setY
    take_step(fn, spread)
    dX = setX - PreviousX
    dY = setY - PreviousY
    search_direction(fn, dX, dY, spread)
    take_step(fn, spread)

def take_step(fn, spread=10):
    search_direction(fn, 1, 0, spread)
    search_direction(fn, 0, 1, spread)

def findMin(fn, attempts):
    for i in range(attempts):
        goHook(fn, spread=10 / (i + 3))
        points.append([setX, setY])

findMin(functionX, 10000)
print(functionX(setX, setY))
print(setX, setY)
with open("points.json", "w") as f:
    json.dump({"function": functionExpr, "points": points}, f)