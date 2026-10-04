CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -I. -I../webserv

MAIN_SRC = main.cpp

WEBSERV_COMMON = ../webserv/HttpRequest.cpp \
                 ../webserv/HttpResponse.cpp \
                 ../webserv/ServerConfig.cpp \
                 ../webserv/LocationConfig.cpp \
                 ../webserv/Client.cpp

HTTP_REQUEST_SRCS = HttpRequest_test.cpp ../webserv/HttpRequest.cpp
HTTP_RESPONSE_SRCS = HttpResponse_test.cpp ../webserv/HttpResponse.cpp

CLIENT_UNIT_SRCS = Client_unit_test.cpp ../webserv/Client.cpp ../webserv/ServerConfig.cpp ../webserv/LocationConfig.cpp ../webserv/HttpRequest.cpp ../webserv/HttpResponse.cpp
CLIENT_INTEG_SRCS = Client_integration_test.cpp $(WEBSERV_COMMON)
CLIENT_SRCS = Client_unit_test.cpp Client_integration_test.cpp $(WEBSERV_COMMON)

SRCS = $(MAIN_SRC) \
       HttpRequest_test.cpp \
       HttpResponse_test.cpp \
       Client_unit_test.cpp \
       Client_integration_test.cpp \
       $(WEBSERV_COMMON)

NAME = test_runner

all: $(NAME)

$(NAME): $(SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(NAME)

test_request: $(MAIN_SRC) $(HTTP_REQUEST_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(HTTP_REQUEST_SRCS) -o test_request

test_response: $(MAIN_SRC) $(HTTP_RESPONSE_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(HTTP_RESPONSE_SRCS) -o test_response

test_client_unit: $(MAIN_SRC) $(CLIENT_UNIT_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(CLIENT_UNIT_SRCS) -o test_client_unit

test_client_integration: $(MAIN_SRC) $(CLIENT_INTEG_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(CLIENT_INTEG_SRCS) -o test_client_integration

test_client: $(MAIN_SRC) $(CLIENT_SRCS) tiny_test.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(MAIN_SRC) $(CLIENT_SRCS) -o test_client

run: $(NAME)
	./$(NAME)

run_request: test_request
	./test_request

run_response: test_response
	./test_response

run_client_unit: test_client_unit
	./test_client_unit

run_client_integration: test_client_integration
	./test_client_integration

run_client: test_client
	./test_client

clean:
	rm -f $(NAME) test_request test_response test_client_unit test_client_integration test_client

fclean: clean

re: fclean all

.PHONY: all run run_request run_response run_client_unit run_client_integration run_client clean fclean re
