CC = g++
LDFLAGS = -lpthread
OUTPUT_BIN = seed

seed: seed_playground.cpp client.cpp server.cpp
	$(CC) -o $(OUTPUT_BIN) seed_playground.cpp client.cpp server.cpp $(LDFLAGS)
	cp $(OUTPUT_BIN) ./seed1
	cp $(OUTPUT_BIN) ./seed2
	cp $(OUTPUT_BIN) ./seed3

seed_playground: seed_playground.cpp
	$(CC) -o seed_playground seed_playground.cpp $(LDFLAGS)
	cp seed_playground ./seed1
	cp seed_playground ./seed2
	cp seed_playground ./seed3

seed_app: SeedApp.cpp client.cpp server.cpp
	$(CC) -o $(OUTPUT_BIN) SeedApp.cpp client.cpp server.cpp $(LDFLAGS)
	cp $(OUTPUT_BIN) ./seed1
	cp $(OUTPUT_BIN) ./seed2
	cp $(OUTPUT_BIN) ./seed3

