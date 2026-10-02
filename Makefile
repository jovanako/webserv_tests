CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -I. -I../webserv

SRCS = HttpRequest_test.cpp ../webserv/HttpRequest.cpp
NAME = test_runner

all: $(NAME)

$(NAME): $(SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(NAME)

run: $(NAME)
	./$(NAME)

clean:
	rm -f $(NAME)

fclean: clean

re: fclean all

.PHONY: all run clean fclean re
