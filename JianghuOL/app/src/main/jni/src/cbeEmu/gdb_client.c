#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>

#define SERVER_PORT 6688
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 1
#define MAX_BREAKPOINTS 32
#define MAX_WATCHPOINTS 16

/* 停止原因定义 */
#define STOP_REASON_TRAP 0x05
#define STOP_REASON_BREAKPOINT 0x07
#define STOP_REASON_WATCHPOINT 0x0A
#define STOP_REASON_SIGNAL 0x00

/* 全局变量 */
TargetSystem gdbTarget;
GDBClient clients[MAX_CLIENTS];
int activeClientsCount;
int server_socket = -1;
pthread_mutex_t target_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 函数声明 */
void init_target_system(TargetSystem *gdbTarget);
void handle_gdb_command(GDBClient *client, const char *command);
void send_gdb_response(GDBClient *client, const char *response);
void *client_thread(void *arg);
int start_server(int port);
void cleanup(void);
void gdb_server_main();

/* 新增辅助函数 */
void disassemble_memory(unsigned int addr, unsigned int count, char *output);
void format_memory_dump(unsigned int addr, unsigned int count, const char *fmt, char *output);
int check_watchpoints(unsigned int addr, unsigned int size, int type);
int check_conditional_breakpoints(unsigned int addr);
void add_watchpoint(unsigned int addr, unsigned int size, int type);
void remove_watchpoint(unsigned int addr);
void add_conditional_breakpoint(unsigned int addr, const char *condition);
void remove_conditional_breakpoint(unsigned int addr);
void backtrace(char *output);
int is_thumb_mode(unsigned int cpsr);

typedef void (*readAllCpuReg)(int *);
typedef void (*gdb_writeReg)(unsigned int reg, unsigned int value);

typedef void (*gdb_readMem)(unsigned int addr, unsigned int length, void *buffer);
typedef void (*gdb_writeMem)(unsigned int addr, unsigned char value);

readAllCpuReg readAllCpuRegFunc;
gdb_readMem gdb_readMemFunc;
gdb_writeMem gdb_writeMemFunc;
gdb_writeReg gdb_writeRegFunc;

/* 初始化目标系统状态 */
void init_target_system(TargetSystem *gdbTarget)
{
    memset(gdbTarget, 0, sizeof(TargetSystem));
    gdbTarget->pc = 0x1000; /* 设置初始PC值 */
    gdbTarget->num_breakpoints = 0;
    gdbTarget->num_watchpoints = 0;
    gdbTarget->last_stop_reason = STOP_REASON_TRAP;
    gdbTarget->step_mode = 0;
    gdbTarget->step_start_addr = 0;
    gdbTarget->step_start_sp = 0;
}

/* 解析并处理GDB命令 */
void handle_gdb_command(GDBClient *client, const char *command)
{
    pthread_mutex_lock(&target_mutex);
    if (strcmp(command, "?") == 0)
    {
        char response[32];
        sprintf(response, "S%02x", gdbTarget.last_stop_reason);
        send_gdb_response(client, response);
    }
    else if (strncmp(command, "g", 1) == 0)
    {
        /* 获取所有寄存器 */
        char response[BUFFER_SIZE];
        int offset = 0;
        unsigned int value;
        if (readAllCpuRegFunc != NULL)
            readAllCpuRegFunc(gdbTarget.registers);
        for (int i = 0; i < 17; i++)
        {
            value = gdbTarget.registers[i];
            for (int b = 0; b < 4; b++)
            {
                offset += sprintf(response + offset, "%02x", (value >> (8 * b)) & 0xFF);
            }
        }

        send_gdb_response(client, response);
    }
    else if (strncmp(command, "m", 1) == 0)
    {
        /* 读取内存 */
        unsigned int addr, length;
        if (sscanf(command, "m%x,%x", &addr, &length) == 2)
        {
            if (gdb_readMemFunc != NULL)
                gdb_readMemFunc(addr, length, gdbTarget.memory);
            char response[BUFFER_SIZE * 2];
            char *ptr = response;

            for (unsigned int i = 0; i < length; i++)
            {
                ptr += sprintf(ptr, "%02x", gdbTarget.memory[i]);
            }

            send_gdb_response(client, response);
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "M", 1) == 0)
    {
        unsigned int addr, value;
        if (sscanf(command, "M%x,1:%x", &addr, &value) == 2)
        {
            if (gdb_writeMemFunc != NULL)
                gdb_writeMemFunc(addr, value);

            send_gdb_response(client, "OK");
        }
    }
    else if (strncmp(command, "P", 1) == 0)
    {
        unsigned int addr, value, valueBig;
        if (sscanf(command, "P%d=%x", &addr, &value) == 2)
        {
            valueBig = value << 24 | ((value & 0x0000ff00) << 8) | ((value & 0x00ff0000) >> 8) | value >> 24;
            if (gdb_writeRegFunc != NULL)
                gdb_writeRegFunc(addr, valueBig);
            send_gdb_response(client, "OK");
        }
    }
    else if (strcmp(command, "s") == 0 || strcmp(command, "interrupt") == 0)
    {
        gdbTarget.simulate_pc_count = 1;
        gdbTarget.running = 1;
        gdbTarget.step_mode = 1;
        gdbTarget.last_stop_reason = STOP_REASON_TRAP;
    }
    else if (strcmp(command, "c") == 0)
    {
        gdbTarget.running = 1;
        gdbTarget.step_mode = 0;
        send_gdb_response(client, "");
    }
    else if (strncmp(command, "Z0", 2) == 0)
    {
        /* 设置软件断点 */
        unsigned int addr;
        if (sscanf(command, "Z0,%x", &addr) == 1)
        {
            if (gdbTarget.num_breakpoints < MAX_BREAKPOINTS)
            {
                gdbTarget.breakpoints[gdbTarget.num_breakpoints++] = addr;
                send_gdb_response(client, "OK");
            }
            else
            {
                send_gdb_response(client, "E03");
            }
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "z0", 2) == 0)
    {
        /* 删除软件断点 */
        unsigned int addr;
        if (sscanf(command, "z0,%x", &addr) == 1)
        {
            for (int i = 0; i < gdbTarget.num_breakpoints; i++)
            {
                if (gdbTarget.breakpoints[i] == addr)
                {
                    for (int j = i; j < gdbTarget.num_breakpoints - 1; j++)
                    {
                        gdbTarget.breakpoints[j] = gdbTarget.breakpoints[j + 1];
                    }
                    gdbTarget.num_breakpoints--;
                    send_gdb_response(client, "OK");
                    pthread_mutex_unlock(&target_mutex);
                    return;
                }
            }
            send_gdb_response(client, "E04");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "Z1", 2) == 0)
    {
        /* 设置硬件断点 */
        unsigned int addr;
        if (sscanf(command, "Z1,%x", &addr) == 1)
        {
            if (gdbTarget.num_breakpoints < MAX_BREAKPOINTS)
            {
                gdbTarget.breakpoints[gdbTarget.num_breakpoints++] = addr;
                send_gdb_response(client, "OK");
            }
            else
            {
                send_gdb_response(client, "E03");
            }
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "z1", 2) == 0)
    {
        /* 删除硬件断点 */
        unsigned int addr;
        if (sscanf(command, "z1,%x", &addr) == 1)
        {
            for (int i = 0; i < gdbTarget.num_breakpoints; i++)
            {
                if (gdbTarget.breakpoints[i] == addr)
                {
                    for (int j = i; j < gdbTarget.num_breakpoints - 1; j++)
                    {
                        gdbTarget.breakpoints[j] = gdbTarget.breakpoints[j + 1];
                    }
                    gdbTarget.num_breakpoints--;
                    send_gdb_response(client, "OK");
                    pthread_mutex_unlock(&target_mutex);
                    return;
                }
            }
            send_gdb_response(client, "E04");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "Z2", 2) == 0)
    {
        /* 设置写观察点 */
        unsigned int addr, size;
        if (sscanf(command, "Z2,%x,%x", &addr, &size) == 2)
        {
            add_watchpoint(addr, size, 1);
            send_gdb_response(client, "OK");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "z2", 2) == 0)
    {
        /* 删除写观察点 */
        unsigned int addr;
        if (sscanf(command, "z2,%x", &addr) == 1)
        {
            remove_watchpoint(addr);
            send_gdb_response(client, "OK");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "Z3", 2) == 0)
    {
        /* 设置读观察点 */
        unsigned int addr, size;
        if (sscanf(command, "Z3,%x,%x", &addr, &size) == 2)
        {
            add_watchpoint(addr, size, 2);
            send_gdb_response(client, "OK");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "z3", 2) == 0)
    {
        /* 删除读观察点 */
        unsigned int addr;
        if (sscanf(command, "z3,%x", &addr) == 1)
        {
            remove_watchpoint(addr);
            send_gdb_response(client, "OK");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "Z4", 2) == 0)
    {
        /* 设置访问观察点 */
        unsigned int addr, size;
        if (sscanf(command, "Z4,%x,%x", &addr, &size) == 2)
        {
            add_watchpoint(addr, size, 3);
            send_gdb_response(client, "OK");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "z4", 2) == 0)
    {
        /* 删除访问观察点 */
        unsigned int addr;
        if (sscanf(command, "z4,%x", &addr) == 1)
        {
            remove_watchpoint(addr);
            send_gdb_response(client, "OK");
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "qRcmd,", 6) == 0)
    {
        /* 监控命令 */
        char cmd[256];
        int cmd_len = strlen(command) - 6;
        if (cmd_len > 0 && cmd_len < sizeof(cmd))
        {
            for (int i = 0; i < cmd_len; i += 2)
            {
                char hex[3] = {command[6 + i], command[6 + i + 1], 0};
                cmd[i / 2] = strtol(hex, NULL, 16);
            }
            cmd[cmd_len / 2] = 0;

            if (strcmp(cmd, "backtrace") == 0 || strcmp(cmd, "bt") == 0)
            {
                char response[BUFFER_SIZE];
                backtrace(response);
                send_gdb_response(client, response);
            }
            else
            {
                send_gdb_response(client, "E01");
            }
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strcmp(command, "qSupported") == 0)
    {
        /* 查询支持的特性 */
        send_gdb_response(client, "PacketSize=4096;QStartNoAckMode+;swbreak+;hwbreak+;vContSupported+");
    }
    else if (strcmp(command, "vCont?") == 0)
    {
        /* 查询支持的继续命令 */
        send_gdb_response(client, "vCont;c;C;s;S;t");
    }
    else if (strncmp(command, "vCont;", 6) == 0)
    {
        /* 继续执行命令 */
        if (strcmp(command + 6, "c") == 0)
        {
            gdbTarget.running = 1;
            gdbTarget.step_mode = 0;
            send_gdb_response(client, "");
        }
        else if (strcmp(command + 6, "s") == 0)
        {
            gdbTarget.simulate_pc_count = 1;
            gdbTarget.running = 1;
            gdbTarget.step_mode = 1;
            gdbTarget.last_stop_reason = STOP_REASON_TRAP;
        }
        else
        {
            send_gdb_response(client, "E01");
        }
    }
    else if (strncmp(command, "qfThreadInfo", 12) == 0)
    {
        /* 查询线程信息 - 单线程 */
        send_gdb_response(client, "m1");
    }
    else if (strncmp(command, "qsThreadInfo", 12) == 0)
    {
        /* 线程信息结束 */
        send_gdb_response(client, "l");
    }
    else if (strncmp(command, "qThreadExtraInfo,", 17) == 0)
    {
        /* 线程额外信息 */
        char info[] = "main_thread";
        char response[BUFFER_SIZE * 2];
        char *ptr = response;
        for (size_t i = 0; i < strlen(info); i++)
        {
            ptr += sprintf(ptr, "%02x", (unsigned char)info[i]);
        }
        send_gdb_response(client, response);
    }
    else if (strncmp(command, "Hc", 2) == 0 || strncmp(command, "Hg", 2) == 0)
    {
        /* 设置线程操作 */
        send_gdb_response(client, "OK");
    }
    else if (strcmp(command, "T") == 0)
    {
        /* 检查线程是否存活 */
        send_gdb_response(client, "OK");
    }
    else
    {
        send_gdb_response(client, "E01");
    }

    pthread_mutex_unlock(&target_mutex);
}

/* 发送GDB响应 */
void send_gdb_response(GDBClient *client, const char *response)
{
    char buffer[BUFFER_SIZE];
    int checksum = 0;
    if (client->active == 0)
    {
        printf("send gdb rsp fail: no active\n");
        return;
    }
    /* 计算校验和 */
    for (const char *p = response; *p; p++)
    {
        checksum += *p;
    }
    checksum &= 0xFF;

    /* 构建响应包 */
    sprintf(buffer, "$%s#%02x", response, checksum);

    /* 发送响应 */
    send(client->socket, buffer, strlen(buffer), 0);
}

/* 客户端处理线程 */
void *client_thread(void *arg)
{
    GDBClient *client = (GDBClient *)arg;
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    int buffer_pos = 0;
    int in_packet = 0;
    int packet_start = 0;
    int packet_len = 0;
    unsigned char checksum = 0;
    unsigned char received_checksum = 0;

    printf("Client connected\n");

    while (client->active)
    {
        /* 接收数据 */
        int bytes_received = recv(client->socket, buffer + buffer_pos,
                                  BUFFER_SIZE - buffer_pos - 1, 0);

        if (bytes_received <= 0)
        {
            /* 客户端断开连接 */
            break;
        }

        buffer_pos += bytes_received;
        buffer[buffer_pos] = '\0';

        /* 解析GDB包 */
        for (int i = 0; i < buffer_pos; i++)
        {
            char tmp = buffer[i];
            if (tmp == '$')
            {
                in_packet = 1;
                packet_start = i + 1;
                packet_len = 0;
                checksum = 0;
            }
            else if (tmp == 0x03)
            {
                // GDB 请求中断
                handle_gdb_command(client, "interrupt");
                // 移除这个字节
                memmove(buffer + i, buffer + i + 1, buffer_pos - i - 1);
                buffer_pos--;
                i--;
            }
            else if (in_packet && tmp == '#')
            {
                in_packet = 0;

                /* 提取接收到的校验和 */
                if (i + 2 < buffer_pos)
                {
                    sscanf(buffer + i + 1, "%02hhx", &received_checksum);

                    /* 验证校验和 */
                    if (checksum == received_checksum)
                    {
                        /* 发送确认 */
                        send(client->socket, "+", 1, 0);

                        /* 提取命令 */
                        memcpy(command, buffer + packet_start, packet_len);
                        command[packet_len] = '\0';

                        /* 处理命令 */
                        handle_gdb_command(client, command);
                    }
                    else
                    {
                        /* 校验和错误 */
                        send(client->socket, "-", 1, 0);
                    }

                    /* 移除已处理的数据 */
                    memmove(buffer, buffer + i + 3, buffer_pos - (i + 3) + 1);
                    buffer_pos -= (i + 3);
                    i = -1; /* 重新开始解析 */
                }
            }
            else if (in_packet)
            {
                checksum += tmp;
                packet_len++;
            }
        }
    }

    /* 清理资源 */
    close(client->socket);
    client->active = 0;
    printf("Client disconnected\n");

    return NULL;
}

/* 启动GDB服务器 */
int start_server(int port)
{
    struct sockaddr_in server_addr;
    int iResult;
    WSADATA wsaData;

    // 初始化Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    /* 创建套接字 */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        printf("socket creation failed with error: %d\n", server_socket);
        return -1;
    }

    /* 设置套接字选项 */
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        printf("setsockopt failed");
        close(server_socket);
        return -1;
    }

    /* 准备服务器地址结构 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    /* 绑定套接字 */
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("bind failed");
        close(server_socket);
        return -1;
    }

    /* 监听连接 */
    if (listen(server_socket, 5) < 0)
    {
        printf("listen failed");
        close(server_socket);
        return -1;
    }

    printf("GDB Server listening on port %d\n", port);
    return 0;
}

/* 清理资源 */
void cleanup(void)
{
    /* 关闭所有客户端连接 */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active)
        {
            clients[i].active = 0;
            close(clients[i].socket);
            pthread_join(clients[i].thread, NULL);
        }
    }
    activeClientsCount = 0;
    /* 关闭服务器套接字 */
    if (server_socket != -1)
    {
        close(server_socket);
    }

    pthread_mutex_destroy(&target_mutex);
}

/* 主函数 */
void gdb_server_main(void)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_socket;

    /* 初始化目标系统 */
    init_target_system(&gdbTarget);

    /* 启动服务器 */
    if (start_server(SERVER_PORT) < 0)
    {
        return 1;
    }

    /* 初始化客户端数组 */
    memset(clients, 0, sizeof(clients));

    /* 信号处理和清理 */
    atexit(cleanup);

    /* 接受客户端连接 */
    while (1)
    {
        /* 查找可用的客户端槽 */
        int client_idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!clients[i].active)
            {
                client_idx = i;
                break;
            }
        }

        if (client_idx == -1)
        {
            /* 没有可用槽位，等待一段时间 */
            sleep(1);
            continue;
        }

        /* 接受新连接 */
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0)
        {
            printf("accept failed");
            continue;
        }

        /* 初始化客户端结构 */
        clients[client_idx].socket = client_socket;
        clients[client_idx].gdbTarget = &gdbTarget;
        clients[client_idx].active = 1;
        activeClientsCount++;
        /* 创建处理线程 */
        if (pthread_create(&clients[client_idx].thread, NULL, client_thread, &clients[client_idx]) != 0)
        {
            printf("thread creation failed");
            close(client_socket);
            clients[client_idx].active = 0;
            activeClientsCount--;
        }
        else
        {
            /* 设置线程为分离状态，自动回收资源 */
            pthread_detach(clients[client_idx].thread);
        }
    }
}

/* 检查是否为Thumb模式 */
int is_thumb_mode(unsigned int cpsr)
{
    return (cpsr & 0x20) != 0;
}

/* 反汇编内存 */
void disassemble_memory(unsigned int addr, unsigned int count, char *output)
{
    char *ptr = output;
    unsigned char code[4];
    unsigned int i;

    for (i = 0; i < count && (ptr - output) < BUFFER_SIZE - 128; i++)
    {
        if (gdb_readMemFunc != NULL)
        {
            gdb_readMemFunc(addr, 4, code);
        }

        unsigned int instr = (code[3] << 24) | (code[2] << 16) | (code[1] << 8) | code[0];
        int thumb = is_thumb_mode(gdbTarget.registers[16]);

        ptr += sprintf(ptr, "0x%08x: ", addr);

        if (thumb)
        {
            unsigned short thumb_instr = (code[1] << 8) | code[0];
            ptr += sprintf(ptr, "%04x  ", thumb_instr);

            if ((thumb_instr & 0xF800) == 0xB000)
            {
                ptr += sprintf(ptr, "b");
            }
            else if ((thumb_instr & 0xFF00) == 0xB500)
            {
                ptr += sprintf(ptr, "push");
            }
            else if ((thumb_instr & 0xFF00) == 0xBD00)
            {
                ptr += sprintf(ptr, "pop");
            }
            else if ((thumb_instr & 0xFFC0) == 0x4400)
            {
                ptr += sprintf(ptr, "add");
            }
            else if ((thumb_instr & 0xFFC0) == 0x4280)
            {
                ptr += sprintf(ptr, "cmp");
            }
            else if ((thumb_instr & 0xFF00) == 0x4800)
            {
                ptr += sprintf(ptr, "ldr");
            }
            else if ((thumb_instr & 0xFF00) == 0x4700)
            {
                ptr += sprintf(ptr, "bx");
            }
            else if ((thumb_instr & 0xFF00) == 0xBE00)
            {
                ptr += sprintf(ptr, "bkpt");
            }
            else
            {
                ptr += sprintf(ptr, "thumb_instr");
            }

            addr += 2;
        }
        else
        {
            ptr += sprintf(ptr, "%08x  ", instr);

            if ((instr & 0x0F000000) == 0x0A000000)
            {
                ptr += sprintf(ptr, "b");
            }
            else if ((instr & 0x0FE00000) == 0x0A000000)
            {
                ptr += sprintf(ptr, "bl");
            }
            else if ((instr & 0x0E100000) == 0x08100000)
            {
                ptr += sprintf(ptr, "ldm");
            }
            else if ((instr & 0x0E100000) == 0x08100000)
            {
                ptr += sprintf(ptr, "stm");
            }
            else if ((instr & 0x0C000000) == 0x04000000)
            {
                ptr += sprintf(ptr, "ldr");
            }
            else if ((instr & 0x0C000000) == 0x04000000)
            {
                ptr += sprintf(ptr, "str");
            }
            else if ((instr & 0x0FE00000) == 0x00000000)
            {
                ptr += sprintf(ptr, "and");
            }
            else if ((instr & 0x0FE00000) == 0x00200000)
            {
                ptr += sprintf(ptr, "eor");
            }
            else if ((instr & 0x0FE00000) == 0x00400000)
            {
                ptr += sprintf(ptr, "sub");
            }
            else if ((instr & 0x0FE00000) == 0x00800000)
            {
                ptr += sprintf(ptr, "add");
            }
            else if ((instr & 0x0FE00000) == 0x01A00000)
            {
                ptr += sprintf(ptr, "mov");
            }
            else if ((instr & 0x0FE00000) == 0x01B00000)
            {
                ptr += sprintf(ptr, "cmp");
            }
            else
            {
                ptr += sprintf(ptr, "arm_instr");
            }

            addr += 4;
        }

        ptr += sprintf(ptr, "\n");
    }
}

/* 格式化内存转储 */
void format_memory_dump(unsigned int addr, unsigned int count, const char *fmt, char *output)
{
    char *ptr = output;
    unsigned char mem[16];
    unsigned int i, j;

    for (i = 0; i < count; i += 16)
    {
        unsigned int len = (count - i) > 16 ? 16 : (count - i);

        if (gdb_readMemFunc != NULL)
        {
            gdb_readMemFunc(addr + i, len, mem);
        }

        ptr += sprintf(ptr, "0x%08x: ", addr + i);

        for (j = 0; j < 16; j++)
        {
            if (j < len)
            {
                ptr += sprintf(ptr, "%02x ", mem[j]);
            }
            else
            {
                ptr += sprintf(ptr, "   ");
            }
            if (j == 7)
            {
                ptr += sprintf(ptr, " ");
            }
        }

        ptr += sprintf(ptr, " |");

        for (j = 0; j < len; j++)
        {
            if (mem[j] >= 32 && mem[j] <= 126)
            {
                ptr += sprintf(ptr, "%c", mem[j]);
            }
            else
            {
                ptr += sprintf(ptr, ".");
            }
        }

        ptr += sprintf(ptr, "|\n");
    }
}

/* 检查观察点 */
int check_watchpoints(unsigned int addr, unsigned int size, int type)
{
    for (int i = 0; i < gdbTarget.num_watchpoints; i++)
    {
        Watchpoint *wp = &gdbTarget.watchpoints[i];
        if (!wp->enabled)
            continue;

        if (addr >= wp->addr && addr < wp->addr + wp->size)
        {
            if ((type == WATCHPOINT_WRITE && (wp->type == WATCHPOINT_WRITE || wp->type == WATCHPOINT_ACCESS)) ||
                (type == WATCHPOINT_READ && (wp->type == WATCHPOINT_READ || wp->type == WATCHPOINT_ACCESS)))
            {
                wp->hit_count++;
                return 1;
            }
        }
    }
    return 0;
}

/* 检查条件断点 */
int check_conditional_breakpoints(unsigned int addr)
{
    for (int i = 0; i < gdbTarget.num_breakpoints; i++)
    {
        if (!gdbTarget.breakpoint_conditional[i] || gdbTarget.breakpoints[i] != addr)
            continue;

        char *condition = gdbTarget.breakpoint_conditions[i];
        if (strlen(condition) == 0)
            return 1;

        unsigned int r0 = gdbTarget.registers[0];
        unsigned int r1 = gdbTarget.registers[1];
        unsigned int r2 = gdbTarget.registers[2];
        unsigned int r3 = gdbTarget.registers[3];

        int result = 0;
        if (strstr(condition, "r0") != NULL)
        {
            unsigned int val;
            if (sscanf(condition, "r0==%x", &val) == 1)
            {
                result = (r0 == val);
            }
            else if (sscanf(condition, "r0!=%x", &val) == 1)
            {
                result = (r0 != val);
            }
            else if (sscanf(condition, "r0>%x", &val) == 1)
            {
                result = (r0 > val);
            }
            else if (sscanf(condition, "r0<%x", &val) == 1)
            {
                result = (r0 < val);
            }
        }
        else if (strstr(condition, "r1") != NULL)
        {
            unsigned int val;
            if (sscanf(condition, "r1==%x", &val) == 1)
            {
                result = (r1 == val);
            }
            else if (sscanf(condition, "r1!=%x", &val) == 1)
            {
                result = (r1 != val);
            }
        }
        else if (strstr(condition, "r2") != NULL)
        {
            unsigned int val;
            if (sscanf(condition, "r2==%x", &val) == 1)
            {
                result = (r2 == val);
            }
            else if (sscanf(condition, "r2!=%x", &val) == 1)
            {
                result = (r2 != val);
            }
        }
        else if (strstr(condition, "r3") != NULL)
        {
            unsigned int val;
            if (sscanf(condition, "r3==%x", &val) == 1)
            {
                result = (r3 == val);
            }
            else if (sscanf(condition, "r3!=%x", &val) == 1)
            {
                result = (r3 != val);
            }
        }

        if (result)
            return 1;
    }
    return 0;
}

/* 添加观察点 */
void add_watchpoint(unsigned int addr, unsigned int size, int type)
{
    if (gdbTarget.num_watchpoints < MAX_WATCHPOINTS)
    {
        Watchpoint *wp = &gdbTarget.watchpoints[gdbTarget.num_watchpoints];
        wp->addr = addr;
        wp->size = size;
        wp->type = type;
        wp->enabled = 1;
        wp->hit_count = 0;
        gdbTarget.num_watchpoints++;
    }
}

/* 删除观察点 */
void remove_watchpoint(unsigned int addr)
{
    for (int i = 0; i < gdbTarget.num_watchpoints; i++)
    {
        if (gdbTarget.watchpoints[i].addr == addr)
        {
            for (int j = i; j < gdbTarget.num_watchpoints - 1; j++)
            {
                gdbTarget.watchpoints[j] = gdbTarget.watchpoints[j + 1];
            }
            gdbTarget.num_watchpoints--;
            break;
        }
    }
}

/* 添加条件断点 */
void add_conditional_breakpoint(unsigned int addr, const char *condition)
{
    if (gdbTarget.num_breakpoints < MAX_BREAKPOINTS)
    {
        gdbTarget.breakpoints[gdbTarget.num_breakpoints] = addr;
        strncpy(gdbTarget.breakpoint_conditions[gdbTarget.num_breakpoints], condition, sizeof(gdbTarget.breakpoint_conditions[0]) - 1);
        gdbTarget.breakpoint_conditions[gdbTarget.num_breakpoints][sizeof(gdbTarget.breakpoint_conditions[0]) - 1] = 0;
        gdbTarget.breakpoint_conditional[gdbTarget.num_breakpoints] = 1;
        gdbTarget.num_breakpoints++;
    }
}

/* 删除条件断点 */
void remove_conditional_breakpoint(unsigned int addr)
{
    for (int i = 0; i < gdbTarget.num_breakpoints; i++)
    {
        if (gdbTarget.breakpoints[i] == addr && gdbTarget.breakpoint_conditional[i])
        {
            for (int j = i; j < gdbTarget.num_breakpoints - 1; j++)
            {
                gdbTarget.breakpoints[j] = gdbTarget.breakpoints[j + 1];
                strncpy(gdbTarget.breakpoint_conditions[j], gdbTarget.breakpoint_conditions[j + 1], sizeof(gdbTarget.breakpoint_conditions[0]));
                gdbTarget.breakpoint_conditional[j] = gdbTarget.breakpoint_conditional[j + 1];
            }
            gdbTarget.num_breakpoints--;
            break;
        }
    }
}

/* 调用栈回溯 */
void backtrace(char *output)
{
    char *ptr = output;
    unsigned int pc = gdbTarget.registers[15];
    unsigned int sp = gdbTarget.registers[13];
    unsigned int lr = gdbTarget.registers[14];
    unsigned int frame_ptr = sp;
    int frame_count = 0;
    int max_frames = 32;

    ptr += sprintf(ptr, "#0  0x%08x in ?? ()\n", pc);

    for (frame_count = 1; frame_count < max_frames && frame_ptr < 0x10000000; frame_count++)
    {
        unsigned int return_addr;
        unsigned int next_frame;

        if (gdb_readMemFunc != NULL)
        {
            gdb_readMemFunc(frame_ptr, 4, (unsigned char *)&return_addr);
            gdb_readMemFunc(frame_ptr + 4, 4, (unsigned char *)&next_frame);
        }

        if (return_addr == 0 || return_addr == 0xFFFFFFFF)
            break;

        ptr += sprintf(ptr, "#%-2d 0x%08x in ?? ()\n", frame_count, return_addr);

        if (next_frame <= frame_ptr || next_frame - frame_ptr > 0x1000)
            break;

        frame_ptr = next_frame;
    }

    if (frame_count >= max_frames)
    {
        ptr += sprintf(ptr, "...\n");
    }
}