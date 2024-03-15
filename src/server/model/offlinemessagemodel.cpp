#include "offlinemessagemodel.hpp"
#include "db.h"
using namespace std;
// 存储用户的离线消息
// 虽然insert传入的参数只有两个，但是msg相当于是js，包含了很多信息
void OfflineMsgModel::insert(int userId, std::string msg)
{
    // 组织sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "insert into OfflineMessage values(%d, '%s')", userId, msg.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userId)
{
    // 组织sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "delete from OfflineMessage where userid=%d", userId);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userId)
{
    // 组织sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "select message from OfflineMessage where userid = %d", userId);

    std::vector<std::string> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);  //res包含了所有找到的数据，有很多行
        if (res != nullptr)
        {
            // 把userid用户的所有消息放入vec中返回
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)  //一行一行遍历得到每行的数据
            {
                vec.push_back(row[0]);  //SQL 查询语句中只select了message，所以是row[0]
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}
