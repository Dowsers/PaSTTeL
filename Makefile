# ========================
# Configuration du projet
# ========================

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -ggdb
LDFLAGS := -pthread

# ========================
# Librairies externes (configurables via variables d'environnement)
# ========================

# Exemple :
#   export PASTTEL=/home/a/Documents/Tools
# Ce dossier doit contenir :
#   $(PASTTEL)/z3/include et $(PASTTEL)/z3/lib
#
# Pour CVC5 (optionnel, peut être dans un autre dossier) :
#   export CVC5_DIR=/path/to/cvc5
# Ce dossier doit contenir :
#   $(CVC5_DIR)/include/cvc5/cvc5.h
#   $(CVC5_DIR)/lib/libcvc5.a

Z3_CFLAGS := -I$(PASTTEL)/include
Z3_LIBS   := -L$(PASTTEL)/lib -lz3

# CVC5_DIR peut être défini via variable d'environnement
# Si non défini, utiliser une valeur par défaut
CVC5_DIR ?= $(PASTTEL)
CVC5_CFLAGS := -I$(CVC5_DIR)/include
CVC5_LIBS   := $(CVC5_DIR)/lib/libcvc5.so $(CVC5_DIR)/lib/libcvc5parser.so $(CVC5_DIR)/lib/libpoly.so.0 $(CVC5_DIR)/lib/libpolyxx.so.0 $(CVC5_DIR)/lib/libpoly.so $(CVC5_DIR)/lib/libpolyxx.so -lgmp


CXXFLAGS += $(Z3_CFLAGS) $(CVC5_CFLAGS)
LDFLAGS  += $(Z3_LIBS) $(CVC5_LIBS)

# ========================
# Répertoires
# ========================

SRC_DIR := src
TEST_DIR := tests
BIN_DIR := bin

# ========================
# Fichiers sources communs
# ========================

COMMON_SRCS := \
	$(SRC_DIR)/affine_term.cpp \
	$(SRC_DIR)/linear_inequality.cpp \
	$(SRC_DIR)/transition.cpp \
	$(SRC_DIR)/lasso_program.cpp \
	$(SRC_DIR)/templates/affine_template.cpp \
	$(SRC_DIR)/templates/nested_template.cpp \
	$(SRC_DIR)/templates/lexicographic_template.cpp \
	$(SRC_DIR)/termination/motzkin_transform.cpp \
	$(SRC_DIR)/termination/ranking_function.cpp \
	$(SRC_DIR)/termination/supporting_invariant.cpp \
	$(SRC_DIR)/smtsolvers/SMTSolverZ3.cpp \
	$(SRC_DIR)/smtsolvers/SMTSolverCVC5.cpp \
	$(SRC_DIR)/termination/ranking_and_invariant_validator.cpp \
	$(SRC_DIR)/termination/termination_analyzer.cpp \
	$(SRC_DIR)/termination/ranking_based_technique.cpp \
	$(SRC_DIR)/nontermination/fixpoint_technique.cpp \
	$(SRC_DIR)/nontermination/geometric_technique.cpp \
	$(SRC_DIR)/nontermination/nontermination_analyzer.cpp \
	$(SRC_DIR)/termination/supporting_invariant_generator.cpp \
	$(SRC_DIR)/termination/generic_termination_synthesizer.cpp \
	$(SRC_DIR)/termination/affine_function_generator.cpp \
	$(SRC_DIR)/parser/smt_parser.cpp \
	$(SRC_DIR)/parser/json_trace_parser.cpp \
	$(SRC_DIR)/linearization/formula_linearizer.cpp \
	$(SRC_DIR)/linearization/uf_handler.cpp \
	$(SRC_DIR)/linearization/array_handler.cpp \
	$(SRC_DIR)/linearization/divmod_handler.cpp \
	$(SRC_DIR)/linearization/nonlinear_mul_handler.cpp \
	$(SRC_DIR)/rewriting/rewrite_let.cpp \
	$(SRC_DIR)/rewriting/rewrite_division.cpp \
	$(SRC_DIR)/rewriting/rewrite_equality.cpp \
	$(SRC_DIR)/rewriting/rewrite_booleans.cpp \
	$(SRC_DIR)/parser/sexpr_utils.cpp \
	$(SRC_DIR)/parser/transition_builder.cpp


COMMON_OBJS := $(COMMON_SRCS:.cpp=.o)

# ========================
# Détection automatique des tests
# ========================

TEST_SRCS := $(wildcard $(TEST_DIR)/test_*.cpp)
TESTS := $(patsubst $(TEST_DIR)/%.cpp, $(BIN_DIR)/%, $(TEST_SRCS))
TEST_OBJS := $(TEST_SRCS:.cpp=.o)

# ========================
# Fichier source principal
# ========================

MAIN_SRC := $(wildcard $(SRC_DIR)/pasttel.cpp)
MAIN := $(patsubst $(SRC_DIR)/pasttel.cpp, $(BIN_DIR)/pasttel, $(MAIN_SRC))
MAIN_OBJ := $(MAIN_SRC:.cpp=.o)

# ========================
# Règles principales
# ========================

.PHONY: all clean test

all: $(TESTS) $(MAIN)
# Compilation des exécutables de test
$(BIN_DIR)/%: $(COMMON_OBJS) $(TEST_DIR)/%.o | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compilation de l'exécutable principal
$(BIN_DIR)/%: $(COMMON_OBJS) $(MAIN_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compilation des fichiers objets communs
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Dossier des binaires
$(BIN_DIR):
	mkdir -p $@

# Exécution de tous les tests
test: $(TESTS)
	@echo "=== Exécution des tests ==="
	@for t in $(TESTS); do \
		echo "--> $$t"; \
		$$t || exit 1; \
	done
	@echo "=== Tous les tests ont réussi ==="

# Nettoyage
clean:
	rm -rf $(COMMON_OBJS) $(TEST_OBJS) $(TESTS) $(MAIN_OBJ) $(MAIN) $(BIN_DIR)

