#import <Foundation/Foundation.h>
#include "mouse_scheme.hpp"

namespace Slic3r { namespace GUI {

static NSString * const kCPChannel = @"CP_MOUSE_SCHEME_CHANNEL";

void send_message_mac(const std::string& msg, const std::string& instance_id)
{
    @autoreleasepool {
        NSDictionary *info = @{
            @"msg":  [NSString stringWithUTF8String:msg.c_str()],
            @"from": [NSString stringWithUTF8String:instance_id.c_str()]
        };
        [[NSDistributedNotificationCenter defaultCenter]
            postNotificationName:kCPChannel
                          object:nil
                        userInfo:info
              deliverImmediately:YES];
    }
}

void register_receive_mac(void (*handler)(const std::string&))
{
    [[NSDistributedNotificationCenter defaultCenter]
        addObserverForName:kCPChannel
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *n){
        NSString *s = n.userInfo[@"msg"];
        if (s && handler) handler(std::string([s UTF8String]));
    }];
}

}} // namespace
