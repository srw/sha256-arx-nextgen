CC      = clang
CFLAGS  = -O3 -march=native -pthread
MFLAGS  = -O3 -march=native -fobjc-arc -framework Foundation -framework Metal -lpthread

.PHONY: all test clean

all: exp_r19_distant_theories exp_r19_metal_gpu symbolic_arx_f2_markov exp_r20_r21_r22_active_algebraic

exp_r19_distant_theories: exp_r19_distant_theories.c
	$(CC) $(CFLAGS) exp_r19_distant_theories.c -o exp_r19_distant_theories

exp_r19_metal_gpu: exp_r19_metal_gpu.m
	$(CC) $(MFLAGS) exp_r19_metal_gpu.m -o exp_r19_metal_gpu

symbolic_arx_f2_markov: symbolic_arx_f2_markov.c
	$(CC) -O3 -march=native symbolic_arx_f2_markov.c -o symbolic_arx_f2_markov

exp_r20_r21_r22_active_algebraic: exp_r20_r21_r22_active_algebraic.c
	$(CC) $(CFLAGS) exp_r20_r21_r22_active_algebraic.c -o exp_r20_r21_r22_active_algebraic

test: all
	./exp_r19_distant_theories
	./exp_r19_metal_gpu
	python3 symbolic_arx_analyzer.py
	./exp_r20_r21_r22_active_algebraic

clean:
	rm -f exp_r19_distant_theories exp_r19_metal_gpu symbolic_arx_f2_markov exp_r20_r21_r22_active_algebraic
