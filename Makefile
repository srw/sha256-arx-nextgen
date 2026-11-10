CC      = clang
CFLAGS  = -O3 -march=native -pthread
MFLAGS  = -O3 -march=native -fobjc-arc -framework Foundation -framework Metal -lpthread

.PHONY: all test clean

all: exp_r19_distant_theories exp_r19_metal_gpu symbolic_arx_f2_markov

exp_r19_distant_theories: exp_r19_distant_theories.c
	$(CC) $(CFLAGS) exp_r19_distant_theories.c -o exp_r19_distant_theories

exp_r19_metal_gpu: exp_r19_metal_gpu.m
	$(CC) $(MFLAGS) exp_r19_metal_gpu.m -o exp_r19_metal_gpu

symbolic_arx_f2_markov: symbolic_arx_f2_markov.c
	$(CC) -O3 -march=native symbolic_arx_f2_markov.c -o symbolic_arx_f2_markov

test: all
	./exp_r19_distant_theories
	./exp_r19_metal_gpu
	python3 symbolic_arx_analyzer.py

clean:
	rm -f exp_r19_distant_theories exp_r19_metal_gpu symbolic_arx_f2_markov
