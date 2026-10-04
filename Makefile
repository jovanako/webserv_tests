CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -I. -I../webserv

MAIN_SRC = main.cpp
HTTP_REQUEST_SRCS = HttpRequest_test.cpp ../webserv/HttpRequest.cpp
HTTP_RESPONSE_SRCS = HttpResponse_test.cpp ../webserv/HttpResponse.cpp
CLIENT_SRCS = Client_test.cpp ../webserv/Client.cpp ../webserv/HttpRequest.cpp ../webserv/HttpResponse.cpp ../webserv/ServerConfig.cpp ../webserv/LocationConfig.cpp

SRCS = $(MAIN_SRC) $(HTTP_REQUEST_SRCS) $(HTTP_RESPONSE_SRCS)
SRCS = $(MAIN_SRC) $(HTTP_REQUEST_SRCS) $(HTTP_RESPONSE_SRCS) Client_test.cpp ../webserv/Client.cpp ../webserv/ServerConfig.cpp ../webserv/LocationConfig.cpp
NAME = test_runner

all: $(NAME)

$(NAME): $(SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(NAME)

test_request: $(MAIN_SRC) $(HTTP_REQUEST_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(HTTP_REQUEST_SRCS) -o test_request

test_response: $(MAIN_SRC) $(HTTP_RESPONSE_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(HTTP_RESPONSE_SRCS) -o test_response

test_client: $(MAIN_SRC) $(CLIENT_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(CLIENT_SRCS) -o test_client

run: $(NAME)
	./$(NAME)

run_request: test_request
	./test_request

run_response: test_response
	./test_response

run_client: test_client
	./test_client

clean:
	rm -f $(NAME) test_request test_response
	rm -f $(NAME) test_request test_response test_client

fclean: clean

re: fclean all

.PHONY: all run run_request run_response clean fclean re
.PHONY: all run run_request run_response run_client clean fclean re
