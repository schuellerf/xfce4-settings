import sys
import io

def before_all(context):
    context.real_stdout = sys.stdout

def before_step(context, step):
    context.step = step
