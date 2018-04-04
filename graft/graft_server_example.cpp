#include "mongoose.h"

#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <deque>

//from s3.h
#define METHOD_GET 2
#define METHOD_POST 2<<1
#define METHOD_PUT 2<<2
#define METHOD_DELETE 2<<3
#define METHOD_PATCH 2<<4
#define METHOD_HEAD 2<<5
#define METHOD_OPTIONS 2<<6

class ClientRequest;
using ClientRequest_ptr = std::shared_ptr<ClientRequest>;

class CryptoNodeSender;
using CryptoNodeSender_ptr = std::shared_ptr<CryptoNodeSender>;

class manager_t
{
	mg_mgr mgr;
	std::deque<ClientRequest_ptr> newClients;
	std::vector<CryptoNodeSender_ptr> cryptoSenders;
public:
	mg_mgr* get_mg_mgr() { return &mgr; }

	void DoWork();

	void OnNewClient(ClientRequest_ptr cr)
	{
		newClients.push_back(cr);
	}
	
	void OnStateChanged(ClientRequest_ptr cr)
	{
		
	}

	void OnCryptoDone(CryptoNodeSender* cns);
};

manager_t manager;

template<typename C>
class StaticMongooseHandler
{
public:	
	static void static_ev_handler(mg_connection *nc, int ev, void *ev_data) 
	{
		C* This = static_cast<C*>(nc->user_data);
		assert(This);
		This->ev_handler(nc, ev, ev_data);
	}
};

class CryptoNodeSender : StaticMongooseHandler<CryptoNodeSender>
{
public:	
	ClientRequest_ptr cr;
	std::string data;
	std::string result;
	
	void send(ClientRequest_ptr cr_, std::string& data_)
	{
		cr = cr_;
		data = data_;
		mg_connection *crypton = mg_connect(manager.get_mg_mgr(),"localhost:1234", static_ev_handler);
		crypton->user_data = this;
		mg_send(crypton, data.c_str(), data.size());
	}
public:	
	void ev_handler(mg_connection* crypton, int ev, void *ev_data) 
	{
		switch (ev) 
		{
		case MG_EV_RECV:
		{
			int cnt = *(int*)ev_data;
			if(cnt<100) break;
			mbuf& buf = crypton->recv_mbuf;
			result = std::string(buf.buf, buf.len);
			crypton->flags |= MG_F_CLOSE_IMMEDIATELY;
			
			manager.OnCryptoDone(this);
		} break;
		default:
		  break;
		}
	}
};

class ClientRequest : public StaticMongooseHandler<ClientRequest>
{
	enum class State
	{
		None,
		ToCrytonode,
		ToThreadPool,
		AnswerError,
		Delete,
//		Stop
	};
	
	using vars_t = std::vector<std::pair<std::string, std::string>>;

	vars_t vars;
	State state;
	mg_connection *client;
	ClientRequest_ptr itself;
public:	
	
	State get_state(){ return state; }
	State set_state(State s){ state = s; }

	ClientRequest(mg_connection *client, vars_t& vars) 
		: vars(vars)
		, state(State::None)
		, client(client)
	{
	}
	
	void setMyPtr(ClientRequest_ptr itself_)
	{
		itself = itself_;
	}
	
	void AnswerOk()
	{
		std::string s("I am answering Ok");
		mg_send(client,s.c_str(), s.size());
//		state = State::Stop;
		client->flags |= MG_F_SEND_AND_CLOSE;
	}

public:
	
	void ev_handler(mg_connection *client, int ev, void *ev_data) 
	{
		assert(client == this->client);
		switch (ev) 
		{
		case MG_EV_POLL:
		{
			if(state == State::ToCrytonode)
			{
				
			}
		} break;
		case MG_EV_CLOSE:
		{
			if(!itself) break;
			state = State::Delete;
//			assert(itself);
			manager.OnStateChanged(itself);
			itself.reset();
		} break;
		default:
		  break;
		}
		
	}
	
};

struct Route
{
	using vars_t = std::vector<std::pair<std::string, std::string>>;
	using Handler = std::function<bool (vars_t&, std::array<char,100>& , std::array<char,100>& ) >;
	
	std::string endpoint;
	int methods;
	Handler handler;
	
	
	Route(std::string endpoint, int methods, Handler handler)
		:endpoint(endpoint), methods(methods), handler(handler)
	{
	}

	static bool route_fun(vars_t& var, std::array<char,100>& input, std::array<char,100>& output)
	{
		return true;
	}
	
	Route(std::string endpoint, int methods)
		:endpoint(endpoint), methods(methods)
		, handler([this](vars_t& var, std::array<char,100>& input, std::array<char,100>& output)->bool 
	{
		return Route::route_fun(var, input, output);
	})
	
	{
	}

};

class Router
{
	Route route = Route("root/aaa/bb", 0);
public:
	Router()
	{
		
	}

	Route* match(const std::string& target, int method)
	{
		return &route;
	}
	
	bool match(const std::string& target, int method, std::vector<std::pair<std::string, std::string>>& vars) const
	{
		vars.clear();
		vars.push_back(std::make_pair("s1","11"));
		vars.push_back(std::make_pair("s2","21"));
		return true;
	}
	
};


class GraftServer final
{
	static Router router;
public:	
	static Router& get_router() { return router; }
	GraftServer()
	{ }
	
	void serve(const char* s_http_port)
	{
		mg_mgr& mgr = *manager.get_mg_mgr();
		mg_mgr_init(&mgr, NULL, 0);
		mg_connection* nc = mg_bind(&mgr, s_http_port, ev_handler);
		mg_set_protocol_http_websocket(nc);
		for (;;) 
		{
			mg_mgr_poll(&mgr, 1000);
		}
		mg_mgr_free(&mgr);
	}

private:
	static void ev_handler(mg_connection *client, int ev, void *ev_data) 
	{
		switch (ev) 
		{
		case MG_EV_HTTP_REQUEST:
		{
			struct http_message *hm = (struct http_message *) ev_data;
			std::string uri(hm->uri.p, hm->uri.len);
			std::string s_method(hm->method.p, hm->method.len);
			int method = (s_method == "GET")? METHOD_GET: 1;
			
			std::vector<std::pair<std::string, std::string>> vars;
			
			if(router.match(uri, method, vars))
			{
				auto ptr = new ClientRequest(client, vars);
//				ptr->mgr = &mgr;
				client->user_data = ptr;
				client->handler = ClientRequest::static_ev_handler;
				manager.OnNewClient( std::shared_ptr<ClientRequest>(ptr) );
			}
			else
			{
				mg_http_send_error(client, 500, "invalid parameter");
				client->flags |= MG_F_SEND_AND_CLOSE;
			}
		} break;
		case MG_EV_POLL:
		{
			manager.DoWork();
		} break;
		default:
		  break;
		}
	}
};

Router GraftServer::router;

void manager_t::DoWork()
{
	if(!newClients.empty())
	{
		ClientRequest_ptr cr = newClients.front(); newClients.pop_front();
		
		CryptoNodeSender_ptr cns = std::make_shared<CryptoNodeSender>();
		cryptoSenders.push_back(cns);
		std::string something(100, ' ');
		{
			std::string s("something");
			for(int i=0; i< s.size(); ++i)
			{
				something[i] = s[i];
			}
		}
		cns->send(cr, something );
	}
}

void manager_t::OnCryptoDone(CryptoNodeSender* cns)
{
	cns->cr->AnswerOk();
}

class cryptoNodeServer
{
public:
	static void run()
	{
		mg_mgr mgr;
		mg_mgr_init(&mgr, NULL, 0);
		mg_connection *nc = mg_bind(&mgr, "1234", ev_handler);
		for (;;) {
		  mg_mgr_poll(&mgr, 1000);
		}
		mg_mgr_free(&mgr);
	}
private:
	static void ev_handler(mg_connection *client, int ev, void *ev_data) 
	{
		switch (ev) 
		{
		case MG_EV_RECV:
		{
			int cnt = *(int*)ev_data;
			if(cnt<100) break;
			mbuf& buf = client->recv_mbuf;
			static std::string data = std::string(buf.buf, buf.len);
			mg_send(client, data.c_str(), data.size());
			client->flags |= MG_F_SEND_AND_CLOSE;
		} break;
		default:
		  break;
		}
	}
};

#include<thread>

int main(int argc, char *argv[]) 
{
	std::thread t(cryptoNodeServer::run);
	
	GraftServer gs; //router);
	Router& router = gs.get_router();
	gs.serve("9080");
	
	t.join();
	return 0;
}

