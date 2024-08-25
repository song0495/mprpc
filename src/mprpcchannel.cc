#include <string>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include "mprpcchannel.h"
#include "mprpcheader.pb.h"
#include "mprpcapplication.h"
#include "zookeeperutil.h"

// header_size + service_name method_name args_size + args
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method, 
                                google::protobuf::RpcController* controller, 
                                const google::protobuf::Message* request,
                                google::protobuf::Message* response, 
                                google::protobuf::Closure* done)
{
    const google::protobuf::ServiceDescriptor* sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();

    // 获取参数的序列化字符串长度 args_size
    uint32_t args_size = 0;
    std::string args_str;
    if (request->SerializeToString(&args_str))
    {
        args_size = args_str.size();
    }    
    else
    {
        controller->SetFailed("Serialize request error!");
        return;
    }

    // 定义rpc的请求header
    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    uint32_t header_size = 0;
    std::string rpc_header_str;
    if (rpcHeader.SerializeToString(&rpc_header_str))
    {
        header_size = rpc_header_str.size();
    }
    else
    {
        controller->SetFailed("Serialize rpc header error!");
        return;
    }

    // 组织待发送的rpc请求的字符串
    std::string send_rpc_str;
    send_rpc_str.insert(0, std::string((char*)&header_size, 4));
    send_rpc_str += rpc_header_str;
    send_rpc_str += args_str;

    // 打印调试信息
    std::cout << "=============================================" << std::endl;
    std::cout << "header_size: " << header_size << std::endl;
    std::cout << "rpc_header_str: " << rpc_header_str << std::endl;
    std::cout << "service_name: " << service_name << std::endl;
    std::cout << "method_name: " << method_name << std::endl;
    std::cout << "args_size: " << args_size << std::endl;
    std::cout << "args_str: " << args_str << std::endl;
    std::cout << "=============================================" << std::endl;

    // 使用tcp编程, 完成rpc方法的远程调用
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        char errtxt[BUFSIZ] = {0};
        sprintf(errtxt, "Creatr socket error! errno: %d", errno);
        controller->SetFailed(errtxt);
        return;
    }

    // 读取配置文件rpcserver的信息
    //std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserver_ip");
    //uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserver_port").c_str());

    ZkClient zkClit;
    zkClit.Start();
    
    // /UserServiceRpc/Login
    std::string method_path = "/" + service_name + "/" + method_name;
    // 127.0.0.1:8888
    std::string host_data = zkClit.GetData(method_path.c_str());
    if (host_data == "")
    {
        controller->SetFailed(method_path + "is not exist!");
        return;
    }
    int idx = host_data.find(":");
    if (idx == -1)
    {
        controller->SetFailed(method_path + "address is invalid!");
        return;
    }
    
    std::string ip = host_data.substr(0, idx);
    uint16_t port = atoi(host_data.substr(idx+1, host_data.size()-idx).c_str());

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    // 连接rpc服务节点
    if (-1 == connect(clientfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)))
    {
        close(clientfd);
        char errtxt[BUFSIZ] = {0};
        sprintf(errtxt, "Connect error! errno: %d", errno);
        controller->SetFailed(errtxt);
        return;
    }

    // 发送rpc请求
    if (-1 == send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
    {
        close(clientfd);
        char errtxt[BUFSIZ] = {0};
        sprintf(errtxt, "Send error! errno: %d", errno);
        controller->SetFailed(errtxt);
        return;
    }
    
    // 接收rpc请求的响应值
    char recv_buf[BUFSIZ] = {0};
    int recv_size = 0;
    if (-1 == (recv_size = recv(clientfd, recv_buf, BUFSIZ, 0)))
    {
        close(clientfd);
        char errtxt[BUFSIZ] = {0};
        sprintf(errtxt, "Recv error! errno: %d", errno);
        controller->SetFailed(errtxt);
        return;
    }
    
    
    /*
    recv_buf中遇到\0后面的数据就无法保存, 导致反序列化失败
    std::string response_str(recv_buf, 0, recv_size);
    if (!response->ParseFromString(response_str))
    */
   
    // 反序列化rpc调用的响应数据
    if (!response->ParseFromArray(recv_buf, recv_size))
    {
        close(clientfd);
        char errtxt[BUFSIZ] = {0};
        sprintf(errtxt, "Parse error! recv_buf: %s", recv_buf);
        controller->SetFailed(errtxt);
        return;
    }

    close(clientfd);
}