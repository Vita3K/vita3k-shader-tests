# Vita3k Shader Tests

This is a collection of vita gpu shader tests. 

## Prerequisites

```
pip install click
```

Set the environment variable `VITA3K_PATH` to the folder where exectuable resides. (e.g. C:\Users\(username)\Documents\dev\Vita3K\build-windows\bin\Debug)

Set the environment variable `VITA3K_DATA` to C:\Users\(username)\AppData\Roaming\Vita3K\Vita3K.

## How to run test

```
python cli.py test (test name)
```

e.g. 

```
python cli.py test vmad16
```

