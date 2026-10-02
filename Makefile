CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -I. -I../webserv

MAIN_SRC = main.cpp
HTTP_REQUEST_SRCS = HttpRequest_test.cpp ../webserv/HttpRequest.cpp
HTTP_RESPONSE_SRCS = HttpResponse_test.cpp ../webserv/HttpResponse.cpp

SRCS = $(MAIN_SRC) $(HTTP_REQUEST_SRCS) $(HTTP_RESPONSE_SRCS)
NAME = test_runner

all: $(NAME)

$(NAME): $(SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(NAME)

test_request: $(MAIN_SRC) $(HTTP_REQUEST_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(HTTP_REQUEST_SRCS) -o test_request

test_response: $(MAIN_SRC) $(HTTP_RESPONSE_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(HTTP_RESPONSE_SRCS) -o test_response

run: $(NAME)
	./$(NAME)

run_request: test_request
	./test_request

run_response: test_response
	./test_response

clean:
	rm -f $(NAME) test_request test_response

fclean: clean

re: fclean all

.PHONY: all run run_request run_response clean fclean re
