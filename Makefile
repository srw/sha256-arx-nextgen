CC      = clang
CFLAGS  = -O3 -march=native -pthread
MFLAGS  = -O3 -march=native -fobjc-arc -framework Foundation -framework Metal -lpthread

.PHONY: all test clean

all: exp_r19_distant_theories exp_r19_carbon_lab bench_carbon_modes bench_r19_record_2ms test_r19_basin_algebraic_engine test_r20_basin_algebraic_engine exp_r19_metal_gpu symbolic_arx_f2_markov exp_r20_distant_theories exp_r20_r21_r22_active_algebraic diag_r21_r22_first_principles exp_r21_r22_carbon_nextgen

exp_r19_distant_theories: exp_r19_distant_theories.c
	$(CC) $(CFLAGS) exp_r19_distant_theories.c -o exp_r19_distant_theories

exp_r19_carbon_lab: exp_r19_carbon_lab.c
	$(CC) $(CFLAGS) exp_r19_carbon_lab.c -o exp_r19_carbon_lab

bench_carbon_modes: bench_carbon_modes.c exp_r19_carbon_lab.c
	$(CC) $(CFLAGS) bench_carbon_modes.c -lm -o bench_carbon_modes

bench_r19_record_2ms: bench_r19_record_2ms.c
	$(CC) $(CFLAGS) bench_r19_record_2ms.c -o bench_r19_record_2ms

test_r19_basin_algebraic_engine: test_r19_basin_algebraic_engine.c
	$(CC) $(CFLAGS) test_r19_basin_algebraic_engine.c -o test_r19_basin_algebraic_engine

test_r20_basin_algebraic_engine: test_r20_basin_algebraic_engine.c
	$(CC) $(CFLAGS) test_r20_basin_algebraic_engine.c -o test_r20_basin_algebraic_engine

exp_r19_metal_gpu: exp_r19_metal_gpu.m
	$(CC) $(MFLAGS) exp_r19_metal_gpu.m -o exp_r19_metal_gpu

symbolic_arx_f2_markov: symbolic_arx_f2_markov.c
	$(CC) -O3 -march=native symbolic_arx_f2_markov.c -o symbolic_arx_f2_markov

exp_r20_distant_theories: exp_r20_distant_theories.c
	$(CC) $(CFLAGS) exp_r20_distant_theories.c -lm -o exp_r20_distant_theories

exp_r20_r21_r22_active_algebraic: exp_r20_r21_r22_active_algebraic.c
	$(CC) $(CFLAGS) exp_r20_r21_r22_active_algebraic.c -o exp_r20_r21_r22_active_algebraic

diag_r21_r22_first_principles: diag_r21_r22_first_principles.c
	$(CC) $(CFLAGS) diag_r21_r22_first_principles.c -lm -o diag_r21_r22_first_principles

exp_r21_r22_carbon_nextgen: exp_r21_r22_carbon_nextgen.c
	$(CC) $(CFLAGS) exp_r21_r22_carbon_nextgen.c -lm -o exp_r21_r22_carbon_nextgen

test: all
	./exp_r19_distant_theories
	./exp_r19_carbon_lab
	./bench_carbon_modes
	./bench_r19_record_2ms
	./test_r19_basin_algebraic_engine
	./test_r20_basin_algebraic_engine
	./exp_r19_metal_gpu
	python3 symbolic_arx_analyzer.py
	./exp_r20_distant_theories
	./exp_r20_r21_r22_active_algebraic
	./diag_r21_r22_first_principles
	./exp_r21_r22_carbon_nextgen

clean:
	rm -f exp_r19_distant_theories exp_r19_carbon_lab bench_carbon_modes bench_r19_record_2ms test_r19_basin_algebraic_engine test_r20_basin_algebraic_engine exp_r19_metal_gpu symbolic_arx_f2_markov exp_r20_distant_theories exp_r20_r21_r22_active_algebraic diag_r21_r22_first_principles exp_r21_r22_carbon_nextgen
