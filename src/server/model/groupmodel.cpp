#include "group_model.hpp"
#include "db.h"

// 创建群组（设置群组名字和描述），插入的是Allgroup表
bool GroupModel::createGroup(Group &group)
{
    // insert into allgroup(groupname, groupdesc) values('chat-server', 'test for create group2');
    char sql[1024] = {0};
    //id是AUTO_INCREMENT的，所以不需要自己插入
    snprintf(sql, sizeof(sql), "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
        group.getName().c_str(), group.getDesc().c_str());
    
    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            group.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }

    return false;
}

// 加入群组（用户ID 加入群组ID 在群组角色），插入的是groupuser表
void GroupModel::addGroup(int userid, int groupid, std::string role)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "insert into groupuser values(%d, %d, '%s')",
        groupid, userid, role.c_str());
    
    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 查询用户所有所在群组信息，返回的是vector<Group>，每个group都是从allgroup和groupuser两张表中拼成的数据
std::vector<Group> GroupModel::queryGroups(int userid)
{
    /**
     * // TODO:MySQL联表查询
     * 1. 先根据userid在groupuser表中查询出该用户所属的groupid
     * 2. 再根据groupid，在AllGroup查询a.id,a.groupname,a.groupdesc
    */
    char sql[1024] = {0};
    //尽量一句sql语句查完所有东西，别写好几条，这个涉及到查询效率
    snprintf(sql, sizeof(sql), "select a.id,a.groupname,a.groupdesc from AllGroup a inner join \
        GroupUser b on a.id = b.groupid where b.userid=%d",
        userid);
    
    std::vector<Group> groupVec;
    
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            // 查出userid所有的群组信息
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                Group group;
                //先只放如id，name，desc，后面再放一堆用户
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                groupVec.push_back(group);
            }
            mysql_free_result(res);
        }
    }

    // 查询群组的用户信息，包含userid，username，userstate
    for (Group &group : groupVec)
    {
        //使用group.getId()方法得到groupID，然后在GroupUser表中得到userId，b.grouprole，然后再在User表得到a.id,a.name,a.state
        snprintf(sql, sizeof(sql), "select a.id,a.name,a.state,b.grouprole from user a \
            inner join groupuser b on b.userid = a.id where b.groupid=%d",
                group.getId());

        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setRole(row[3]);
                group.getUsers().push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return groupVec;
}

// 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
std::vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                idVec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return idVec;  
}