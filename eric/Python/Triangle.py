from math import sqrt
functionExpr = "(3 - x)**2 + 7*(y - x**2)**2"
def fn(C):
    x, y = C
    return eval(functionExpr)
tri = [[0, 2],[-1, -1],[1, -1]]
def midpoint(a, b):
    return [(a[0]+b[0])/2, (a[1]+b[1])/2]
def sub(a, b):
    return [a[0]-b[0], a[1]-b[1]]
def FlipTri():
    global tri
    tri = sorted(tri, key=fn)
    NewP = midpoint(tri[0], tri[1])
    direction = sub(NewP, tri[2])
    newPoint = [tri[2][0] + 2*direction[0], tri[2][1] + 2*direction[1]]
    tri[2] = newPoint

for i in range(10):
    print(tri)
    FlipTri()

